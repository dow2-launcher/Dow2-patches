Mac add-ons - the native Mac game (Dawn of War II: Retribution by Feral, Mac 1.0.3, x86_64)

One add-on = one dylib = one tick, as on Windows (docs/ADDON_RULES.md). The Elite launcher for macOS installs each one from
launcher.json "components_macos" into ~/Library/Application Support/EliteLauncher/mac_addons/<id>/ (the dylib + its ini) and
starts the game with DYLD_INSERT_LIBRARIES = crossplay_fix.dylib first, then the ticked add-ons. crossplay_fix (Mac crossplay +
protfix) lives in dow2-launcher/Dow2-mac-crossplay; without it in the game an add-on changes nothing (common/machook.h).

  common/machook.h     the shared base: log and ini next to the dylib, game check (LC_UUID of 1.0.3 + the bytes of every site),
                       protfix check, writes into the game's code / vtables
  nohints/             NoHints.dylib       store id nohints_mac       (Windows: nohints/)
  reinforcefix/        ReinforceFix.dylib  store id reinforcefix_mac  (Windows: reinforcefix/)
  herolabelfix/        HeroLabelFix.dylib  store id herolabelfix_mac  (Windows: herolabelfix/)
  overwatchonce/       OverwatchOnce.dylib store id overwatchonce_mac (Windows: overwatchonce/)
  ctrlattack/          CtrlAttack.dylib    store id ctrlattack_mac    (Windows: ctrlattack/; key Option, not Ctrl - Feral makes
                                           Control + click a right click)
  lobbystats/          LobbyStats.dylib    store id lobbystats_mac    (Windows: lobbystats/, the lobby part; own_rating.h shared;
                                           HTTPS with the system's CFNetwork, http_mac.h; test_net.c = CI's network check)
  retreatnow/          RetreatNow.dylib    store id retreatnow_mac    (Windows: retreatnow/; CHANGES THE SIMULATION - every player of
                                           the match needs RetreatNow 4, Windows or Mac; experiment, off by default)
  build.sh             builds them all into mac/out/ (CI: the "mac" job of .github/workflows/build.yml, artifact dow2-mac-addons)

Each writes <Name>.log next to itself and reads <Name>.ini there ([<Name>] Enabled=1, as on Windows). Addresses are the 1.0.3
binary's (names from its symbol map in the comments); another build is left alone and logged.
Store entries: components_macos, id <addon>_mac, games ["vanilla", "elite"], off by default; put up by hand (elite-release does
not carry components_macos).
