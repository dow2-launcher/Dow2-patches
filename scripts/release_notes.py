"""Release notes helper for CI.

Two logs, two readers:
  CHANGELOG.md              - for us: what changed in the code, with commits and details (## X.Y.Z entries)
  changelog/<X.Y.Z>/en.md   - for the players: what changed for them, in plain words (ru.md next to it). The GitHub Release
                              is made from it, and the launcher shows it in its news (ru.md for Russian players).
                              How to write it: docs/ADDON_TEXTS.md.

  release_notes.py check <version>   fails when CHANGELOG.md has no entry or changelog/<version>/ has no en.md / ru.md
  release_notes.py notes <version>   prints the player notes of every version since the previous X.Y.0 up to this one
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "CHANGELOG.md"
PLAYERS = ROOT / "changelog"
HEADING = re.compile(r"^## (\d+)\.(\d+)\.(\d+)\b.*$", re.M)


def parse_version(text):
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", text)
    if not match:
        sys.exit(f"not a plain X.Y.Z version: {text}")
    return tuple(map(int, match.groups()))


def name(version):
    return ".".join(map(str, version))


def dev_versions():
    return {tuple(map(int, h.groups())) for h in HEADING.finditer(CHANGELOG.read_text(encoding="utf-8"))}


def player_versions():
    if not PLAYERS.is_dir():
        return set()
    return {parse_version(f.name) for f in PLAYERS.iterdir()
            if re.fullmatch(r"\d+\.\d+\.\d+", f.name) and (f / "en.md").is_file()}


def release_versions(version):
    have = player_versions()
    previous_release = max((v for v in dev_versions() if v[2] == 0 and v < version), default=None)
    picked = [v for v in have if v <= version and (previous_release is None or v > previous_release)]
    return sorted(picked, reverse=True)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("check", "notes"):
        sys.exit(__doc__)
    version = parse_version(sys.argv[2])
    if version not in dev_versions():
        sys.exit(f"CHANGELOG.md has no entry for {sys.argv[2]}")
    for lang in ("en", "ru"):
        if not (PLAYERS / sys.argv[2] / f"{lang}.md").is_file():
            sys.exit(f"changelog/{sys.argv[2]}/{lang}.md is missing: what changed for the players, in plain words")
    if sys.argv[1] == "notes":
        print("\n\n".join(f"## {name(v)}\n\n" + (PLAYERS / name(v) / "en.md").read_text(encoding="utf-8").strip()
                          for v in release_versions(version)))


if __name__ == "__main__":
    main()
