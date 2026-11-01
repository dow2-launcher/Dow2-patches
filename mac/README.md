# Native Mac (Feral) port fixes - work in progress (paused 30.10.2026)

- `elitefix.c` -> `elitefix.dylib`, loaded with `DYLD_INSERT_LIBRARIES`: the join check (modDllCRC of the Windows SimEngine.dll)
  and a buffer overrun of the Feral material loader on Elite models. Built with osxcross (x86_64 clang).
- `rngtrace.c` (built into the dylib with `-DRNGTRACE`): logs every call of the sim random generator with its seed, index and
  return addresses, to line a Mac game up with a Windows one and find where a desync starts (the Mac makes extra calls in SMCC).
