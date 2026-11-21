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

### Vue Instance (tout se règle ici)
- Spectre de la piste (gris), spectres des pistes qui la creusent (à leur couleur), courbe de réduction (bleu).
- **Pastilles de couleur** en haut : une par piste qui creuse celle-ci (+ **Self**). Clic = régler cette relation ;
  croix = retirer ; clic droit = bypass ; **« + »** = ajouter une piste ou **Self** (une nouvelle instance démarre en Self).
- **Directement sur le graphique** (relation sélectionnée) :
  - bords de la zone : glisser **↔ = fréquence**, **↕ = pente** (vers le haut = plus raide), comme sur Pro-MB ;
  - ligne / courbe pointillée de couleur = seuil (en « Seuil auto », elle suit le spectre moyen de la source),
    façonnable par points comme la courbe max ; poignée **≡** = **knee** (seuil soft) ;
  - **courbe bleue = réduction max**, façonnable comme un EQ : double-clic dessus = ajouter un point,
    glisser = fréquence / profondeur, molette = largeur, double-clic ou clic droit sur un point = supprimer ;
  - double-clic sur un bord ou sur la poignée du seuil = valeur par défaut, **Shift** = réglage fin,
    **Ctrl + glisser** sur un point = largeur.
- **Panneau de droite** : interrupteur de **bypass** de la relation (elle reste en place), Quantité, Attaque (jusqu'à
  300 ms), Release, Résolution, **tilt bas | haut** pour Détail, Attaque et Release (glisser ↕), **64 | 128 bandes**,
  **Soft / Hard** (Hard : réduction franche jusqu'à 70 dB), **Masking** (ne creuser que là où les deux
  pistes ont de l'énergie), **Seuil auto**, **Suivre la hiérarchie**.
- **Self** : avec un seuil fixe = compresseur multibande sans latence ; avec **Seuil auto** = suppresseur de
  résonances (seuls les pics au-dessus du spectre lissé de la piste sont creusés).
- **En bas** : Dry/Wet, **Make-up** (+ Auto), Lookahead (+ interrupteur **Auto** = calé sur le délai de synchro),
  Stéréo (**L/R ou M/S**, **Link**, **Focus**), Delta, Bypass. En bypass (ou Bypass all), l'affichage se grise.

### Vue Matrice (vue d'ensemble)
- **Hiérarchie** (à gauche) : glissez-déposez pour changer l'ordre. ★ = instance maîtresse (porte la macro
  **Breath** et **Bypass all** ; c'est sur *sa* piste qu'on les automatise).
- **Grille libre** : colonne = piste qui creuse, ligne = piste creusée. Toutes les combinaisons sont possibles.
  - sous la diagonale : relations « hiérarchiques » (la piste du dessus creuse celle du dessous) ;
  - au-dessus de la diagonale : relations **libres** (point blanc), actives quel que soit l'ordre
    (ex. la piste 5 creuse un peu la piste 2) ;
  - clic = sélectionner, double-clic = activer / désactiver ; la couleur montre ce qui est retiré en direct.
- **Suivre la hiérarchie** (réglage de chaque relation) : activé = la relation suit l'ordre (et l'automation
  de Priorité la fait basculer) ; désactivé = relation libre, toujours active.

### Automation
Chaque piste expose des emplacements « Self » + 8 sources, nommés d'après la piste qui creuse
(ex. « Kick – Quantité », « Kick – Seuil ») : la Quantité et le seuil global de chaque relation s'automatisent
dans Ableton, comme Breath, Dry/Wet, Make-up, Link, Focus, Bypass, Priorité…

### Barre du haut (partout)
**Famille** (α Alpha, β Bêta, γ Gamma, σ Sigma, ω Oméga — clic sur la lettre) : seules les instances d'une même
famille se voient et se creusent ; chaque famille a sa macro, son Bypass all et son instance maîtresse ·
Undo / Redo · Snapshots **A / B** (+ **A→B** pour copier A dans B ; les modifications vont dans le snapshot actif) ·
macro **Breath** (0–100 %) · **Bypass all**.

---

## 4. Comment ça marche (choix importants)

- **Zéro latence par défaut.** Le son n'est jamais traité par FFT : la FFT ne sert qu'à mesurer les pistes du
  dessus ; l'atténuation est faite par 64 filtres en cloche à phase minimale.
- **Export identique à l'écoute.** Chaque analyse est datée par sa position dans la timeline. Pour que
  l'export (souvent plus rapide que le temps réel, piste par piste) sonne exactement comme la lecture,
  une piste lit l'analyse des pistes du dessus avec un **petit délai de synchro = 1 buffer audio**
  (affiché sous « Lookahead », ex. 5,3 ms à 256 échantillons / 48 kHz).
  Avec le **Lookahead ≥ ce délai**, le retard est entièrement compensé (« Réaction sans retard »).
- **Pas de boucle de rétroaction** : chaque piste est analysée *avant* son propre traitement, donc deux pistes
  peuvent se creuser mutuellement sans emballement.
- **Plusieurs pistes sources** : on garde, bande par bande, la **réduction la plus forte** (pas la somme) :
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
