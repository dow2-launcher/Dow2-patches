# Native Mac (Feral) port fixes - work in progress (paused 30.10.2026)

- `elitefix.c` -> `elitefix.dylib`, loaded with `DYLD_INSERT_LIBRARIES`: the join check (modDllCRC of the Windows SimEngine.dll)
  and a buffer overrun of the Feral material loader on Elite models. Built with osxcross (x86_64 clang).
- `rngtrace.c` (built into the dylib with `-DRNGTRACE`): logs every call of the sim random generator with its seed, index and
  return addresses, to line a Mac game up with a Windows one and find where a desync starts (the Mac makes extra calls in SMCC).

## Mac <-> Windows crossplay (`crossplay_fix.dylib`)
- `v8/` - early sources (randfix, mathfix, msmath), kept for history.
- `v25/` - current sources (09.11.2026). The dylib is injected into the Feral Mac DOW2 (3.19.10320) and patches the simulation
  so it computes bit-for-bit like Windows DOW2.exe (x87 PC_24 math, reciprocal-vs-divide, summation order, sorts as STLport,
  RNG floats, ...). Each `*fix.c` is one dylib part with its site table (`*_sites.h`) and a self-test (`*_st*.inc`) that runs
  through the patched code at load; most can be switched off with `DOW2_NO<NAME>=1`. `protfix.c` stops the Feral
  anti-modification timer from quitting a patched game 15-18 min after start.
  - `build.sh` builds with osxcross on rts; `reg25.sh` runs the 1M side-by-side tests against DOW2.exe (Wine) per fix;
    `lift/` holds the lifted Windows x87 code used as reference; `README_RU.txt` is the player readme of the package.
  - `difftest/` - the replay checker (`difftest.py`: Mac simulation vs Windows on recorded replays, sync CRC per frame;
    `diagnose_high.py`: full-state dumps to find the first differing field) and `HANDOFF.md`, the full step-by-step log.
- Status v25: 1M tests all 0; 9 reference replays Low-equal to the end (incl. outskirts 03-15, fixed by rowfix); the full
  56-replay run with v25 and angelgate 2025-12-13 (F1335, FOW query order) are still open. Not released to players.
