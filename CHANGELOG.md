# Journal des versions de Breath

Chaque version est téléchargeable pour toujours dans l'onglet **Releases** du dépôt
(publiée automatiquement quand le numéro de version change).
Les projets enregistrés avec une version sont relus par les versions suivantes.
Quand une version **change le son** à réglages identiques, c'est indiqué par ⚠️.

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
