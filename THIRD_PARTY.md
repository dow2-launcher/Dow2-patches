# Third-party files

The MIT license in [LICENSE](LICENSE) covers the code and texts written for this repository. It does **not** cover the
third-party files below: they remain under their own licenses / with their owners.

| Files | What | License / owner |
|---|---|---|
| `dxvk/DXVK_d3d9.bin` (not in git: fetched by `make`, see `blobs.json`) | DXVK 2.7.1 x32 `d3d9.dll`, the official release renamed | zlib license, Philip Rebohle and the DXVK contributors - `dxvk/DXVK_LICENSE.txt` |
| `soundfix/SoundFix_fmodex.bin` (not in git: fetched by `make`) | FMOD Ex 4.32.20 `fmodex.dll` (the file Elite Mod ships as `Elite_bin/Elite_fmodex.bin`) | proprietary, Firelight Technologies |
| `soundbugtest/SoundBugTest_fmodex.bin` (not in git: fetched by `make`) | FMOD Ex 4.32.3 `fmodex.dll` from the game's Steam depot | proprietary, Firelight Technologies |
| `eliteai/mod/` (`*.ai`, `*.scar`) | AI scripts that replace / extend the game's and Elite Mod's AI scripts at the same paths | the original parts: Relic Entertainment / SEGA (Warhammer 40,000: Dawn of War II - Retribution) and the Elite Mod authors |
| addresses, structures and names of `DOW2.exe`, `Spooge.dll`, `Shark.dll` and other game files in the sources and docs | reverse-engineering notes for interoperability | the game itself is not included; it belongs to Relic Entertainment / SEGA, Games Workshop |

Warhammer 40,000 and Dawn of War are trademarks of Games Workshop Limited; Dawn of War II is a game by Relic Entertainment
published by SEGA. This project is not affiliated with them.
