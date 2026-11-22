# Idées et retours à traiter dans les prochains builds

Liste tenue à jour au fil des retours. Rien de ce qui est ici n'est encore codé.

## Seuil (retours du 20/11)
- [x] (V0.2) **Courbe de seuil façonnable comme la courbe max** : pouvoir y ajouter des points (bells),
      les déplacer, régler leur largeur (molette), les supprimer.
- [x] (V0.2) **Pente / largeur du seuil** : traitée par le knee réglable (à confirmer avec l'usage).
- [x] (V0.2) **Seuil « soft » (knee réglable)** : la réduction démarre progressivement autour du seuil
      au lieu de s'enclencher d'un coup (knee de 0 dB = dur, jusqu'à ~24 dB = très doux).
      *« nid » = knee : confirmé dans la conversation.*

## Automation (retour du 21/11) — très important
- [x] (V0.3, Quantité + seuil global) **Tout doit être automatisable dans le DAW.**
  Aujourd'hui seuls quelques réglages d'instance le sont (Priorité, Breath, Bypass, Bypass all, Delta, Dry/Wet,
  Make-up, Make-up auto, Stéréo). Les réglages de relation vivent dans la mémoire partagée et ne sont pas
  visibles par le DAW.
  Piste retenue : chaque instance expose des « emplacements de relation » fixes (ex. 8 sources + Self) avec
  tous leurs réglages (on/off, Quantité, Attaque, Release, Résolution, Détail low/high, Soft/Hard, Masking,
  Seuil auto, plage basse/haute, pentes, seuil, réduction max, knee…), nommés d'après la piste source.
  L'automation est lue par l'instance creusée → export toujours identique au temps réel.
  Cas particuliers à trancher : Lookahead (change la latence), Famille, 64/128 bandes, points des courbes.
  **Décisions (21/11)** : pas d'automation pour le Lookahead ni pour la résolution (64/128 bandes).
  Priorité : **Quantité** et **seuil global** (niveau de la courbe de seuil / décalage du seuil auto) de chaque
  relation automatisables. Le reste des réglages de relation suivra le même mécanisme si c'est simple.

## Retours du 21/11 (inspirés de Soothe 3 et FabFilter)
### Tilt (attaque / release / détail)
- [x] (V0.3) **Tilt d'attaque et de release** : différence de vitesse entre le bas et le haut du spectre,
      de ×0,1 à ×10 de chaque côté. Widget comme Soothe 3 : deux petites cases (bas | haut) avec une courbe,
      réglées en glissant ↕ — pas des potards. Défaut ×1 (aucun changement de son pour les anciens projets).
- [x] (V0.3) Passer **Détail low / high** sur le même widget « tilt » (cohérence visuelle).
### Stéréo
- [x] (V0.3) Section stéréo façon Soothe : mode **L/R** ou **M/S** ; **Link** (0–100 %) qui lie gauche/droite ou mid/side ;
      **Focus** pour cibler plutôt un côté (mid ou side, gauche ou droite) ou les deux (ex. « 53 | 100 »).
      Remplace le sélecteur Stéréo / Mid / Side actuel (conversion des anciens projets à prévoir).
### Affectations
- [x] (V0.3) **État initial d'une nouvelle instance : Self activé.**
- [x] (V0.3) **Petite croix en haut à droite de chaque pastille** (Self, Kick…) pour retirer l'affectation.
- [x] (V0.3) Clic droit sur une pastille : ne garder que le **bypass**, et afficher le menu **sous la pastille**
      (aujourd'hui il apparaît sous le graphe).
### Graphique
- [x] (V0.3) **Étiquettes** : seuil à droite, max à gauche ; **poignée du knee** de l'autre côté (aujourd'hui cachée par la
      courbe max).
- [x] (V0.3) **Ctrl + glisser sur un point** (courbe max et courbe seuil) = régler sa largeur (Q), en plus de la molette.
- [x] (V0.3) **Shift = réglage fin partout** (convention FabFilter), à la place d'Alt.
- [x] (V0.3) **Mode Hard : −70 dB tout en bas du graphe** (aujourd'hui −70 dB est au milieu, la moitié basse ne sert à rien).
### Réglages
- [x] (V0.3) **Attaque jusqu'à 300 ms** (défaut inchangé : 5 ms ; les sessions existantes gardent leurs valeurs).
- [x] (V0.3) **64 / 128 bandes** déplacé à côté du réglage Résolution (dans le panneau de relation).
### Noms de piste
- [x] (V0.3) **Vrais noms des pistes Ableton** au lieu de « Piste 1, Piste 2 ». Piste : Ableton envoie bien la couleur ;
      Breath efface peut-être le nom quand un message n'apporte que la couleur → ne mettre à jour le nom que s'il est
      fourni. À vérifier dans Ableton ; sinon le renommage manuel (double-clic) reste la solution.

## Retours du 22/11
- [x] (V0.4) Nouveau plugin **Breath One** (l'ancien Breath figé en V0.3).
- [x] (V0.4) Résolution : plus de plateau quand rien ne dépasse le seuil (étalement « par maximum »).
- [x] (V0.4) Titre « BREATH ONE » pointillé, fenêtre 15 % plus grande, onglet « Ensemble ».
- [ ] Onglet Ensemble : régler aussi les réglages propres à chaque piste (Dry/Wet, Make-up, Stéréo…).
