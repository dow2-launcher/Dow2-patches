ShellFix.dll v1 - the game no longer closes a second into a match when Elite's ShellPatch.dll could not hook in
(Dawn of War II: Retribution 3.19, Elite 3.0-3.2).

Elite's ShellPatch.dll shows the race meter above the command panel. When it starts it changes the game's HUD code in four
places and wraps one function of it; the wrapper needs a little memory in the lower 2 GB of the game's address space. On a
computer where that part is already full when ShellPatch starts (big video drivers - an Intel and an NVIDIA driver both in
the game -, the game's own reservations, every add-on loaded before it), the wrapper is silently missing and every match
closes the game on its first frame (the game's crash reporter closes it, no Windows error).

ShellFix waits until ShellPatch.dll has started and, only in that broken state, puts the missing wrapper in itself, as
ShellPatch would have done. When ShellPatch hooked in fine (almost always), another ShellPatch build is there, or the game is
not 3.19, it changes nothing. Nothing in the simulation changes (no desync).

Install: put ShellFix.dll into <game>\Elite_DLLs\ (or tick it in Elite Launcher add-ons). Log: Elite_DLLs\ShellFix.log
("REPAIRED: ..." = it was needed). Settings: ShellFix.ini [ShellFix] Enabled=0 turns it off.
