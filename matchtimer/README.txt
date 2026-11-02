MatchTimer.dll v5 - match clock (MM:SS) in Dawn of War II: Retribution 3.19, Elite and vanilla.

Shows how long the match has been going, in game time: 12:34, after an hour 1:02:34, right under the Menu button, in the
game's own font and the colour of the "Menu" label. It is text of the game's own Menu button movie (Scaleform), set by the
game's interface thread - nothing is drawn over the game, no Direct3D (v1-v4 drew it through Direct3D; gone in v5).
It starts when the match starts (after the loading screen) and stands still while the game does (pause, an offline game
minimized, "waiting for players"), so everyone in the match and the replay see the same time. In every game: multiplayer,
offline vs AI, replays (OnlineOnly=1 in the ini keeps it to live multiplayer). Never in menus, on the loading or
post-game screen. Only you see it: the DLL reads the game, it changes nothing in the simulation, so it can't desync anyone.

Install: the launcher's "Match timer" tick; by hand, MatchTimer.dll (and MatchTimer.ini if you want other settings) into
<game>\dow2-patches\ (loaded by the winmm shim).
Settings: MatchTimer.ini (FontSize, Color, Shadow, OnlineOnly). The game picks up a saved change within a second.
Log: MatchTimer.log (next to the dll). Turn off: MatchTimer.ini -> Enabled=0.

Needs DoW2 Retribution 3.19 (Steam); 32-bit. On any other build it does nothing and says so in the log.
