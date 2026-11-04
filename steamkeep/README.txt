SteamKeep.dll v2 - the match goes on when Steam goes away, to its very end (Dawn of War II: Retribution 3.19, Elite). Experimental.

What the game does by itself: when the connection to Steam drops (Steam servers lost, Steam client restarted or
crashed, logged out, logged in elsewhere) the game leaves the match it is playing - a "lost connection to Steam" dialog
and back to the menu, or for a dead Steam client a Windows message box and the game closes. When the Steam client shuts
down or logs off it also asks the game window to close, and the game quits to the desktop at once. The match itself
does not need Steam (it runs over the relay), so the player could just play on.

SteamKeep, only while a game is running (not in the menus, not in replays):
 - Steam going away is not turned into "leave the match": you stay in the game and play to the end, however long Steam
   is away and however often it drops and comes back;
 - once Steam went away in the match, its request to close the game is ignored until the match is over. Your own ways
   out still work: Alt+F4, Quit in the game menu, the window's close button / taskbar Close.
After the match the game is not closed (the result and the replay go out as usual); when Steam is back the game logs
in again by itself. If Steam stays away, the game may not start again until Steam is back - that is Steam, not the game.
Nothing in the simulation changes, so there is no desync.
Not covered: a match over Steam P2P (no relay) - its connections close with Steam; no internet at all.

Install: put SteamKeep.dll into <game>\Elite_DLLs\ and add ", SteamKeep.dll" to InjectedFiles in Elite.launchconfig
(or tick it in Elite Launcher add-ons). Log: Elite_DLLs\SteamKeep.log. Settings: SteamKeep.ini (Enabled, Online,
Offline, IgnoreSteamClose). Turn off: SteamKeep.ini [SteamKeep] Enabled=0.
