# Breath

Plugin VST3 de priorisation fréquentielle multi-instance (Windows + Mac).

- Architecture : [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Le plugin est compilé automatiquement par GitHub Actions à chaque modification
  (onglet **Actions** du dépôt → dernière exécution « Build » → section **Artifacts**).

## Installer sur Windows (Ableton Live)

1. Télécharger l'artifact **Breath-Windows** et le dézipper.
2. Copier le dossier `Breath.vst3` dans `C:\Program Files\Common Files\VST3\`.
3. Ableton : *Options → Préférences → Plug-ins* → activer
   **« Utiliser les dossiers système VST3 »**, puis cliquer **Rescan**.
4. Le plugin apparaît dans le navigateur : *Plug-ins → VST3 → Breath*.
