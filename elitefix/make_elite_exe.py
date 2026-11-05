#!/usr/bin/env python3
"""make_elite_exe.py <original Elite.exe> <out Elite.exe> - the Elite.exe of the elitefix_wmic add-on (elitefix-nowmic-2).

Elite's starter (Elite.exe, 64-bit, "Universal launcher for Dawn of War II mods", the same file in Elite 3.0.1 / 3.1 / 3.2,
md5 ba5b6bdd...) does this once the game's window is there (main function, 0x14002095d on):
  1. starts a detached thread 0x140008f40 = the game watch: every second it looks for DOW2.exe, and exit(1)s the starter as
     soon as the game is gone (its OpenProcess(0x200) + GetPriorityClass never succeeds, so it never stops on its own);
  2. checks the game's command line with `wmic process where processid=N get CommandLine` (0x140007ff0, boost::process);
  3. sleeps 30 s, then joins its splash thread (Elite.bmp in a STATIC window: its message loop never gets WM_QUIT).
So the starter only ever ends through 1. Windows 11 25H2 has no wmic: 2 throws, nobody catches it, abort() = the c0000409
crash at Elite.exe+0x78da5 (elitefix-nowmic-1 fixed that).

elitefix-nowmic-1 jumped from 0x14002095d straight to 3 - past 1 as well: no game watch, the starter waited for its splash
thread forever and stayed in the task manager after every game, closed or crashed (BeArD, fb 242, 04.11.2026; the launcher
then also thinks the game still runs). elitefix-nowmic-2 jumps only past 2: at 0x140020976, right after the watch thread
is started and detached, `jmp 0x140020c31` (the 30 s sleep). Everything else is the original file.
"""
import hashlib, sys

ORIG_MD5 = "ba5b6bdd72b373fd874cd4c917f938f2"
OUT_MD5 = "eb6b56d55a8e353d69634b9630857f78"
OFF = 0x1FD76                              # file offset of VA 0x140020976 (.text: VA 0x140001000 = file 0x400)
OLD = bytes.fromhex("8bd3488d8d5002")      # mov edx,ebx; lea rcx,[rbp+0x250] (the wmic check's arguments)
NEW = b"\xe9" + (0x140020C31 - (0x140020976 + 5)).to_bytes(4, "little")   # jmp 0x140020c31


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n")[0])
    b = bytearray(open(sys.argv[1], "rb").read())
    if hashlib.md5(b).hexdigest() != ORIG_MD5:
        sys.exit(f"{sys.argv[1]}: not Elite's original Elite.exe (md5 {hashlib.md5(b).hexdigest()}, want {ORIG_MD5})")
    if b[OFF:OFF + len(OLD)] != OLD:
        sys.exit(f"unexpected bytes at 0x{OFF:x}: {b[OFF:OFF + len(OLD)].hex()}")
    b[OFF:OFF + len(NEW)] = NEW
    if hashlib.md5(b).hexdigest() != OUT_MD5:
        sys.exit(f"result md5 {hashlib.md5(b).hexdigest()} != {OUT_MD5}")
    open(sys.argv[2], "wb").write(b)
    print(f"{sys.argv[2]}: md5 {OUT_MD5} (elitefix-nowmic-2)")


if __name__ == "__main__":
    main()
