# Breath — Plan d'architecture

> Statut : **implémenté** (étapes 1 à 5). Écarts par rapport au plan initial, décidés pendant le développement :
> - Analyse sur une grille de **128 échantillons** (gains recalculés tous les 32), alignée sur la timeline.
> - **Deux FFT** : courte (≈ 21 ms) pour tout le spectre, longue (≈ 85 ms, recalculée tous les 512
>   échantillons) pour les graves sous ~300 Hz, afin de bien séparer kick et basse.
> - **Délai de synchro** = plus grand buffer temps réel observé (arrondi à 64), commun à toutes les
>   instances et sauvegardé dans le projet pour que l'export reste identique.
> - Chaque instance **vide ses analyses** à chaque nouveau départ de lecture (évite de lire une analyse
>   périmée d'une lecture précédente pendant un export).
> - Mode stéréo : **Stéréo / Mid / Side**. « Mid + Side » a été retiré : la détection venant des pistes du
>   dessus, Mid et Side recevraient la même réduction, ce qui revient exactement au mode Stéréo.
> - Snapshots : le snapshot actif enregistre automatiquement les modifications (comparaison A/B façon Logic).
> - Test automatique : **sortie export = sortie temps réel, à l'échantillon près** (écart mesuré : 0).

## 1. Vue d'ensemble

```
 Piste "Kick"            Piste "Basse"           Piste "Voix"
┌──────────────┐        ┌──────────────┐        ┌──────────────┐
│ Breath #1    │        │ Breath #2    │        │ Breath #3    │
│  Analyse FFT │──┐     │  Analyse FFT │──┐     │  Analyse FFT │
│  Filtres IIR │  │     │  Filtres IIR │  │     │  Filtres IIR │
└──────────────┘  │     └──────────────┘  │     └──────────────┘
        ▲         ▼             ▲         ▼             ▲
        │   ┌──────────────────────────────────────────┐│
        └───│   BreathHub (mémoire partagée unique,    │┘
            │   dans le processus du DAW)              │
            │  • annuaire des instances (max 16)       │
            │  • analyses horodatées (lock-free)       │
            │  • matrice : hiérarchie + relations      │
            │  • snapshots A/B/C + undo/redo           │
            └──────────────────────────────────────────┘
```

Chaque instance de Breath fait trois choses :
1. **Publier** l'analyse spectrale de sa propre piste dans le Hub.
2. **Lire** les analyses des pistes situées au-dessus d'elle (selon la matrice).
3. **Atténuer** sa propre piste avec une banque de filtres IIR dynamiques (zéro latence).

Le son de la piste n'est **jamais** traité par FFT : la FFT ne sert qu'à mesurer.

## 2. Outils et structure du projet

| Élément | Choix |
|---|---|
| Langage | C++20 |
| Framework | JUCE **9.0.5** (dernière version stable), téléchargé automatiquement par CMake |
| Build | CMake ≥ 3.25 |
| Format | VST3 uniquement |
| Windows | Compilé par GitHub Actions (aucun outil à installer) |
| Mac | GitHub Actions, binaire universel arm64 + x86_64 |
| Tests | Exécutable de tests C++ (sans DAW), lancé aussi par GitHub Actions |
| Police | Inter (libre, embarquée dans le plugin) |

```
Breath/
├─ CMakeLists.txt
├─ src/
│  ├─ PluginProcessor.*        point d'entrée audio
│  ├─ PluginEditor.*           point d'entrée interface
│  ├─ hub/                     BreathHub, annuaire, matrice, ring buffers
│  ├─ dsp/                     analyseur FFT, banque de filtres, lookahead, M/S
│  ├─ model/                   relations, snapshots, undo/redo, sauvegarde
│  └─ ui/                      vue Instance, vue Matrice, thème, composants
├─ tests/                      tests automatiques (dont test export = temps réel)
├─ resources/fonts/            Inter
└─ .github/workflows/          compilation Mac (et Windows en bonus)
```

## 3. Communication entre instances (BreathHub)

- **Singleton partagé** : un objet unique (`juce::SharedResourcePointer<BreathHub>`) créé par la
  première instance et détruit avec la dernière. Comme toutes les instances d'un DAW sont chargées
  depuis le même fichier `.vst3` dans le même processus, elles voient toutes le même objet.
- **Thread audio sans lock** : chaque instance possède un emplacement fixe (16 max) contenant un
  *ring buffer* d'analyses. Écriture par l'instance propriétaire uniquement, lecture par les autres,
  avec des compteurs atomiques (schéma « seqlock » : le lecteur vérifie que la trame n'a pas été
  réécrite pendant sa lecture). Aucune allocation, aucun mutex sur le thread audio.
- **Matrice** (hiérarchie, relations, réglages) : modifiée uniquement sur le thread de l'interface.
  À chaque modification, une nouvelle copie immuable est publiée par échange de pointeur atomique ;
  le thread audio lit toujours une copie complète et cohérente. Les anciennes copies sont libérées
  plus tard, hors du thread audio.
- **Identité** : chaque instance a un identifiant unique (UUID) sauvegardé dans son état.
  - *Ajout* : l'instance s'inscrit dans le Hub et apparaît en bas de la hiérarchie.
  - *Suppression* : elle se désinscrit ; ses relations sont gardées en mémoire (pour l'undo).
  - *Duplication* : la copie arrive avec le même UUID qu'une instance déjà vivante → elle en reçoit
    un nouveau automatiquement et se place juste sous l'original, relations désactivées par défaut.
  - *Fermeture/réouverture du projet* : **chaque** instance sauvegarde la matrice complète (+ snapshots)
    dans son état avec un numéro de version. À la réouverture, la version la plus récente gagne et
    les relations sont reliées par UUID.
- **Avertissement d'isolement** : si une instance reste seule dans le Hub plus de quelques secondes
  alors que son état sauvegardé mentionne d'autres instances (ou que l'utilisateur l'attend), un
  petit bandeau discret s'affiche : « Aucune autre instance détectée — votre DAW isole peut-être les
  plugins ». Pas de mode de secours.

## 4. Export fiable (rendu offline = écoute temps réel)

C'est le point le plus délicat. Principe : **tout ce que calcule Breath dépend uniquement de la
position dans la timeline, jamais de la taille des blocs ni de l'ordre dans lequel le DAW traite
les pistes.**

1. **Grille fixe** : l'analyse est découpée en trames de 64 échantillons alignées sur la timeline
   (trame *k* = échantillons `64k … 64k+63`). Une trame est publiée avec son numéro.
   Les gains des filtres sont aussi recalculés sur cette grille de 64 échantillons, et tous les
   lissages se font échantillon par échantillon. Résultat : découper l'audio en blocs de 32, 512
   ou 2048 donne exactement le même son.
2. **Lecture exacte** : pour traiter la position *p*, une instance lit la trame qui correspond
   exactement à *p − D* chez chaque piste du dessus (*D* = délai de synchronisation, voir 4).
3. **Attente en offline** : si `isNonRealtime()` et que la trame n'est pas encore publiée, l'instance
   attend (attente active courte puis petites pauses), avec un **délai max de 2 s** par bloc. Si le
   délai est dépassé (DAW qui rend les pistes une par une), on continue sans bloquer et l'instance
   affiche « Export non synchronisé » dans l'interface.
4. **Le compromis inévitable (à valider)** : en temps réel, quand le DAW traite le bloc *n*, il est
   garanti que toutes les pistes ont fini le bloc *n−1* (le DAW doit les mixer), mais **pas** le
   bloc *n* (pistes traitées en parallèle, dans un ordre quelconque). Pour que le résultat soit
   identique à l'export, il faut donc lire l'analyse avec un délai *D* d'au moins un buffer audio :
   - *D* = taille du buffer du DAW en temps réel (ex. 256 échantillons ≈ 5 ms à 48 kHz), mémorisé
     et commun à toutes les instances, et réutilisé tel quel pendant l'export ;
   - concrètement, sans lookahead, l'atténuation réagit **≈ 1 buffer en retard** ;
   - avec le **lookahead** activé et ≥ *D*, ce retard est entièrement compensé (et l'atténuation peut
     même anticiper les transitoires). C'est le mode recommandé pour les kicks très percussifs.
5. **Changement de position** (lecture, boucle, saut) : détecté par discontinuité de la position ;
   les ring buffers et les états des filtres sont remis à zéro de façon identique dans les deux modes.
   Transport arrêté : un compteur interne prend le relais (pas d'enjeu d'export).
6. **Test automatique** : un programme de test crée 4 instances virtuelles dans le même processus,
   les fait tourner (a) « en temps réel » avec des tailles de blocs variables et un ordre de
   traitement aléatoire sur plusieurs threads, (b) « en offline » avec d'autres tailles de blocs,
   et vérifie que les sorties sont **identiques à l'échantillon près**.

## 5. Hiérarchie et matrice

- Paramètre automatisable **Priorité** (1 à 16) par instance. Le glisser-déposer dans la matrice
  modifie ces paramètres (le DAW les voit donc comme de vrais changements de paramètre).
- **Égalité de rang — règle stable** : on trie par Priorité, puis, à égalité, par **ordre de
  création** de l'instance (numéro attribué une fois pour toutes et sauvegardé). L'instance la plus
  ancienne passe au-dessus. Cette règle ne dépend ni du temps ni de l'ordre de traitement du DAW,
  donc elle donne le même résultat en export.
- **Transitions lissées** : chaque relation possède un « poids » de 0 à 1. Quand la hiérarchie
  change, le poids glisse en ~150 ms (lissage par échantillon, aligné sur la timeline) : la
  réduction apparaît/disparaît en fondu, sans saut.
- **Relations** : une case on/off par couple « supérieure → inférieure ». Par défaut à la création
  : activée (modifiable).
- **Synchronisation** : la matrice vit dans le Hub, donc toute modification faite depuis n'importe
  quelle instance est vue immédiatement par toutes.
- **Undo/Redo** : un historique unique, partagé par toutes les instances (dans le Hub).
- **Snapshots A/B/C** : copie complète (ordre, relations, réglages de relation, macro) ; rappel en un
  clic avec transition lissée.
- **Heatmap** : chaque relation publie sa réduction moyenne courante (atomique) ; la matrice colore
  la case en conséquence.

## 6. Moteur d'atténuation

### Analyse (pistes du dessus)
- FFT avec fenêtre de Hann, 64 bandes logarithmiques fixes de 20 Hz à 20 kHz (FFT plus longue pour les
  graves, plus courte pour les aigus, afin d'avoir à la fois précision et réactivité).
- Enveloppe par bande en dB, publiée dans le Hub sur la grille de 64 échantillons.

### Calcul de la réduction pour une relation (ex. Kick → Basse)
Pour chaque bande dans la plage [Fréquence basse, Fréquence haute] :
`réduction = Quantité × (niveau du Kick − Seuil)`, limitée à **Réduction max**, puis lissée par
**Attaque/Release**, multipliée par la macro **Breath** et par le poids de transition.

**Résolution** (bouton continu) : la courbe de réduction calculée sur 64 bandes est « floutée » le
long des fréquences. Au minimum, tout est moyenné → une seule bande large (compression sidechain
large bande sur la plage choisie). Au maximum, chaque bande est indépendante (spectral fin). Ce
choix rend le bouton parfaitement continu et sans clic, contrairement à un changement brutal du
nombre de filtres.

### Combinaison de plusieurs pistes du dessus — choix proposé : **maximum par bande**
Si Kick demande −4 dB à 60 Hz et Voix demande −2 dB à 60 Hz sur la Basse, on applique −4 dB.
Pourquoi : (1) la réduction ne s'emballe jamais quand plusieurs pistes du dessus jouent ensemble ;
(2) chaque réglage « Réduction max » reste un vrai garde-fou lisible ; (3) c'est prévisible : la
case la plus « chaude » de la heatmap explique ce qu'on entend. (Une somme plafonnée creuse
davantage mais rend chaque relation dépendante des autres.)

### Application (piste courante)
- Banque de filtres « en cloche » (peak EQ, biquads IIR à phase minimale, **zéro latence**),
  64 au maximum, positionnés sur les 64 bandes ; seules les bandes de la plage active sont calculées.
- Coefficients mis à jour toutes les 64 échantillons (sur la grille), gains lissés → pas de clic.
- **Mode stéréo** : Stéréo lié (même gain sur L et R) ou Mid/Side (traiter Mid, Side ou les deux).
- **Dry/Wet**, **Delta** (= signal d'entrée − signal traité), **Bypass** (avec fondu).
- **Lookahead** 0–10 ms (désactivé par défaut) : l'audio est retardé, la latence est déclarée au DAW
  (`setLatencySamples`), et le signal Dry est retardé de la même valeur → Dry/Wet aligné.
- **Global** : macro Breath (0–100 %) et Bypass All. Pour rester exact à l'export, ces deux valeurs
  sont publiées dans les trames horodatées ; la valeur qui fait foi est celle de l'**instance
  maîtresse** (★, par défaut la première créée, modifiable dans la matrice). Tourner le bouton
  depuis une autre instance pilote en réalité le paramètre de l'instance maîtresse — c'est donc sur
  la piste de la maîtresse que l'automation s'écrit.

## 7. Identification

- Nom et couleur de piste récupérés via le mécanisme VST3 « track info » (`updateTrackProperties`
  dans JUCE) quand le DAW le fournit (Cubase, Studio One, Reaper, Bitwig… ; Ableton et FL ne le
  fournissent que partiellement).
- Champ de renommage manuel : un nom saisi à la main a priorité sur le nom du DAW.

## 8. Visualisation et interface

- Le thread audio pousse ses spectres dans des buffers lock-free ; l'interface les lit à ~60 fps
  (synchronisée sur l'écran, `VBlankAttachment`). Le thread audio ne dessine jamais rien.
- Vue **Instance** : spectre de la piste + spectres des pistes du dessus (à leur couleur), courbe de
  réduction, réglages de l'instance.
  **Réglage graphique des relations** directement sur le spectre : des pastilles choisissent la
  relation éditée (ex. Kick → Basse) ; sa zone de fréquences est surlignée à la couleur de la piste
  du dessus. Bords gauche/droit = plage de fréquences, ligne bleue = réduction max, ligne pointillée
  à la couleur de la piste = seuil de déclenchement. Les autres relations restent visibles en
  pointillés discrets (clic dessus = sélection). Double-clic = valeur par défaut, Alt/Option = réglage
  fin. Tout est synchronisé avec le panneau de la vue Matrice et passe par l'undo/redo.
- Vue **Matrice** : liste réordonnable par glisser-déposer, grille des relations avec heatmap,
  panneau de réglages de la relation sélectionnée, snapshots A/B/C, undo/redo, macro, Bypass All.
- Redimensionnable (coin en bas à droite, ratio conservé), dessin 100 % vectoriel → net à toutes
  les tailles et sur écrans Retina/4K.
- DA : fond gris très sombre (mode sombre façon Logic), texte Inter, coins arrondis 10–12 px, un seul
  accent discret (bleu système), animations douces (150–250 ms).

## 9. Performances visées

- 16 instances max. Coût principal : les filtres en cloche (au pire 64 × 2 canaux par instance).
  Optimisations : seules les bandes de la plage active et dont la réduction est non nulle sont
  calculées ; FFT d'analyse partagée (une seule par instance, quel que soit le nombre de relations).
- Objectif : 10 instances < ~10 % d'un cœur de processeur récent.

## 10. Limites connues (à connaître dès maintenant)

- **VST3 seulement ⇒ pas « tous les DAW »** : Logic Pro et GarageBand n'acceptent que l'AU ;
  Pro Tools n'accepte que l'AAX. JUCE peut produire l'AU presque gratuitement (même code).
- **Mémoire partagée** : fonctionne si le DAW charge les plugins dans un même processus (Ableton,
  FL Studio, Cubase/Nuendo, Studio One, Reaper par défaut, Reason…). Bitwig (selon le mode
  d'hébergement) ou Reaper avec « exécuter dans un processus séparé » isolent les plugins →
  avertissement.
- Si deux projets sont ouverts en même temps dans le même DAW, leurs instances se verraient
  mutuellement (cas rare).
- **Licence JUCE** : gratuite pour un usage personnel / petits revenus ; à vérifier avant toute
  vente.

## 11. Étapes

1. Instance seule qui compile, s'ouvre dans le DAW, laisse passer le son, interface vide redimensionnable.
2. Hub + annuaire + matrice + synchronisation export (+ test automatique export = temps réel).
3. Moteur d'atténuation + réglages par relation et globaux.
4. Visualisation, heatmap, snapshots, undo/redo, finition de l'interface.
5. Compilation Mac via GitHub Actions + explication de l'installation sur Mac.
