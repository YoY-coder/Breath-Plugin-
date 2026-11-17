# Breath

Plugin VST3 de **priorisation fréquentielle multi-instance** (Windows + Mac).
On place Breath sur plusieurs pistes, on définit une hiérarchie (ex. 1. Kick, 2. Basse, 3. Voix…) :
chaque piste « creuse » les pistes situées en dessous d'elle, uniquement là où elles se gênent.

- Architecture technique : [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Le plugin est compilé et testé automatiquement par GitHub à chaque modification.

---

## 1. Télécharger

Onglet **Actions** du dépôt → dernière ligne **« Build »** avec une coche verte → section **Artifacts** :

- **Breath-Windows** : dossier `Breath.vst3`
- **Breath-macOS** : `Breath-mac.zip` (binaire universel Apple Silicon + Intel)

## 2. Installer

### Windows
1. Dézipper → copier le dossier `Breath.vst3` dans `C:\Program Files\Common Files\VST3\`.

### Mac
1. Dézipper jusqu'à obtenir `Breath.vst3`.
2. Finder → **Cmd + Maj + G** → `~/Library/Audio/Plug-Ins/VST3` → y glisser `Breath.vst3`.
3. Le plugin n'est pas signé par Apple : ouvrir le **Terminal** et coller (une fois par nouvelle version) :
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Breath.vst3
   ```

### Ableton Live
*Préférences → Plug-ins* → activer **« Use VST3 Plug-In System Folders »** → **Rescan**.
Breath apparaît dans *Plug-ins → Breath*. Si vous remplacez une ancienne version, fermez Ableton avant de copier.

---

## 3. Utiliser Breath

### Mise en place
1. Mettez Breath sur chaque piste concernée (jusqu'à 16). Les instances se trouvent toutes seules.
2. Chaque nouvelle instance se place en bas de la hiérarchie. Son nom et sa couleur viennent de la piste
   du DAW quand celui-ci les fournit (double-clic sur le nom en haut à gauche pour renommer).

### Vue Matrice (bouton « Matrice » en haut)
- **Hiérarchie** (à gauche) : glissez-déposez les pistes pour changer l'ordre. La petite barre bleue
  montre en direct combien chaque piste est creusée.
- **★ Instance maîtresse** : cliquez à droite d'un nom pour choisir l'instance qui porte la macro
  **Breath** et **Bypass all**. C'est sur *sa* piste qu'on automatise ces deux réglages.
- **Grille « qui creuse qui »** : ligne = piste creusée, colonne = piste qui creuse.
  - clic = choisir la relation à régler ;
  - double-clic = activer / désactiver la relation (case en pointillés = désactivée) ;
  - la couleur s'allume selon ce qui est retiré en direct (heatmap).
- **Panneau de droite** : réglages de la relation sélectionnée (Quantité, Réduction max, Seuil,
  Attaque, Release, Plage de fréquences, Résolution).

### Vue Instance
- Spectre de la piste (gris), spectres des pistes du dessus (à leur couleur), courbe de réduction (bleu).
- Les pastilles en haut choisissent la relation à régler **directement sur le graphique** :
  - bords de la zone colorée = plage de fréquences ;
  - ligne bleue = réduction max ;
  - ligne pointillée de couleur = seuil (la piste du dessus ne déclenche que là où sa courbe dépasse cette ligne) ;
  - double-clic = valeur par défaut, **Alt / Option** = réglage fin.
- En bas : Dry/Wet, Lookahead, mode Stéréo / Mid / Side, Delta (écouter ce qui est retiré), Bypass.

### Barre du haut (partout)
Undo / Redo · Snapshots **A / B / C** (vos modifications sont enregistrées dans le snapshot actif ;
cliquez sur un autre pour basculer) · macro **Breath** (0–100 %) · **Bypass all**.

---

## 4. Comment ça marche (choix importants)

- **Zéro latence par défaut.** Le son n'est jamais traité par FFT : la FFT ne sert qu'à mesurer les pistes du
  dessus ; l'atténuation est faite par 64 filtres en cloche à phase minimale.
- **Export identique à l'écoute.** Chaque analyse est datée par sa position dans la timeline. Pour que
  l'export (souvent plus rapide que le temps réel, piste par piste) sonne exactement comme la lecture,
  une piste lit l'analyse des pistes du dessus avec un **petit délai de synchro = 1 buffer audio**
  (affiché sous « Lookahead », ex. 5,3 ms à 256 échantillons / 48 kHz).
  Avec le **Lookahead ≥ ce délai**, le retard est entièrement compensé (« Réaction sans retard »).
- **Plusieurs pistes du dessus** : on garde, bande par bande, la **réduction la plus forte** (pas la somme) :
  la réduction ne s'emballe jamais et chaque « Réduction max » reste un vrai garde-fou.
- **Égalité de rang** (paramètre Priorité automatisé) : à rang égal, l'instance **créée en premier** passe
  au-dessus. Les changements de hiérarchie se font en fondu (~150 ms).

## 5. Limites connues

- VST3 uniquement : pas de Logic Pro / GarageBand (AU) ni de Pro Tools (AAX).
- Les instances doivent être chargées dans le même processus. Si le DAW isole les plugins (ex. Bitwig en mode
  sandbox, Reaper « processus séparé »), un avertissement s'affiche.
- **Freeze / Flatten / export d'une piste seule** : les autres pistes ne tournent pas, la piste est rendue sans
  leur influence (un avertissement « Export : … » s'affiche).
- L'automation de Priorité / Breath est lue une fois par buffer (précision ≈ 1 buffer).

## 6. Pour les développeurs

```
cmake -S . -B build -DBREATH_BUILD_TOOLS=ON
cmake --build build --target BreathTests && build/BreathTests      # tests automatiques
cmake --build build --target BreathScreenshot                       # captures de l'interface
```
