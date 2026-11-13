# DoW2 Retribution: Mac <-> Windows crossplay (vanilla)

Lets the native Mac version of Dawn of War II: Retribution (Feral) play multiplayer with Windows players.
`crossplay_fix.dylib` is loaded into the game at start: it lets the Mac game join Windows lobbies and makes
the Mac simulation compute exactly like the Windows game, so matches stay in sync.

- Requirements: the Steam Mac version of Dawn of War II: Retribution, vanilla (no mods). Steam running and signed in.
- Install: unzip anywhere, run `run_vanilla.command` (the first time: right-click -> Open).
  If the game is not in the default Steam folder: `GAME=/path/to/the/game/folder ./run_vanilla.command`.
- Uninstall: delete the folder. Game files are not changed (the script only puts a `steam_appid.txt`
  next to the game binary so Steam does not restart the game without the patch; it can stay or be deleted).
- No launcher needed.
- Elite mod: not supported yet.
- Log: `/tmp/crossplay_fix.log`.

Verify a download (GitHub build provenance, needs the `gh` CLI):
`gh attestation verify dow2mac_crossplay_v26-vanilla.zip -R dow2-launcher/Dow2-patches`

Build: `mac/build.sh` on macOS with clang 19 (CI: `.github/workflows/mac.yml`). License: MIT (`LICENSE`).
