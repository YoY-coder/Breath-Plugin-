# Journal des versions

**Breath One** (nouveau plugin, numérotation propre, étiquettes « One-VX.Y ») — puis plus bas l'ancien **Breath** (V0.1–V0.3, figé).

Chaque version est téléchargeable pour toujours dans l'onglet **Releases** du dépôt
(publiée automatiquement quand le numéro de version change).
Les projets enregistrés avec une version sont relus par les versions suivantes.
Quand une version **change le son** à réglages identiques, c'est indiqué par ⚠️.

## Breath One V0.1 (nouveau plugin)
- Nouveau nom et nouvelle identité : **Breath One**. L'ancien Breath reste figé en V0.3 pour les projets existants.
- ⚠️ (par rapport à Breath) **Résolution** : plus de plateau quand rien ne dépasse le seuil ; la réduction s'étale
  seulement autour des endroits qui dépassent.
- Onglet **Ensemble** : toutes les instances en direct, réglables depuis un seul plugin.
- Titre « BREATH ONE » dans le style du logo DROZ Recordz, fenêtre 15 % plus grande à l'ouverture.

---
# Ancien Breath (figé)

## V0.3
- Automation : Quantité et seuil global de chaque relation (emplacements Self + 8 sources nommés d'après la source).
- Tilt bas | haut pour Détail, Attaque, Release (×0,1 à ×10).
- Stéréo L/R ou M/S avec Link et Focus.
- Croix sur les pastilles, clic droit = bypass ; nouvelle instance en Self.
- Shift = réglage fin, Ctrl + glisser = largeur des points, échelle Hard jusqu'en bas, étiquettes réorganisées.
- Attaque jusqu'à 300 ms ; 64 | 128 bandes déplacé ; noms de piste conservés.
- Aucun changement de son pour les projets V0.1 / V0.2.

## V0.2
- Courbe de seuil façonnable par points (comme la courbe max) : ajout, déplacement, largeur, suppression.
- Seuil « soft » : knee réglable sur le graphique (0 à 24 dB), avec bande de visualisation.
- Aucun changement de son pour les projets V0.1.

## V0.1
Première version numérotée. Elle regroupe tout le travail fait jusqu'ici :
- Instances qui se trouvent toutes seules (mémoire partagée), jusqu'à 16, réparties en 5 familles
  (α Alpha, β Bêta, γ Gamma, σ Sigma, ω Oméga).
- Export identique à l'écoute en temps réel (vérifié par test automatique, à l'échantillon près).
- Relations libres (grille N × N), hiérarchie par glisser-déposer, Priorité automatisable, Self.
- Par relation : Quantité, Attaque, Release, Résolution, Détail low / high, Soft / Hard (jusqu'à 70 dB),
  Masking, Seuil auto, Suivre la hiérarchie, bypass ; sur le graphique : plage, pentes des bords,
  seuil, courbe de réduction max façonnable par points.
- Par instance : Dry/Wet, Make-up (+ auto), Lookahead (+ auto), Stéréo / Mid / Side, 64 ou 128 bandes,
  Delta, Bypass (relié au bouton d'activation du DAW).
- Globaux par famille : macro Breath, Bypass all, instance maîtresse. Snapshots A / B (+ copie A→B), undo / redo.
