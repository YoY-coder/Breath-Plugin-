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
- [ ] **Tout doit être automatisable dans le DAW.**
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
