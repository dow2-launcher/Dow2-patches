Stand 1 test of JumpRetry (03.11.2026), run from /mnt/dow2bench/dev/vanilla_scar_test next to run.sh/gatetest.lua:
  bash stand_lock.sh jumpretry-test bash ./hold_jr.sh      (vanilla skirmish, -dev; JumpRetry.dll in dow2-patches)
autarch_ui.scar spawns an Autarch every 16 s, selects him and points the camera at a target 72 m away (an obstacle on
the way); jr_trial.sh <n> 1033 653 [move|stop] clicks his jump button and the screen centre through the HUD.
autarch.scar is the earlier SCAR-only repro (Cmd_SquadAbilityPos, no HUD - JumpRetry doesn't see those orders).
Results: without the DLL (Enabled=0) the jump is lost after the hop (5 Ability -> 0 Idle, energy 100); with it
"fell out of the ability without jumping" -> retry 1/2 -> state 15 Jump, lands ~4 m from the target (6 of 6 trials);
a right-click move (command 44) or the Stop hotkey (43, BindingHalt) during the walk cancels the retry.
