"""Player texts of the add-ons (docs/store-texts.json) -> a launcher.json and/or addons.json.

  store_texts.py check                     docs/store-texts.json is complete (5 languages, a known category, an order number)
  store_texts.py addons                    writes the texts into addons.json (its add-ons only)
  store_texts.py store <launcher.json>     shows what would change in that store file
  store_texts.py store <launcher.json> -w  writes it (then sign it as usual: elite-publish sign ...)

Only category, order, name, description are touched; versions, files, options stay as they are.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXTS = ROOT / "docs" / "store-texts.json"
ADDONS = ROOT / "addons.json"
LANGS = ("en", "ru", "es", "fr", "de")
CATEGORIES = ("network", "interface", "gameplay", "fixes", "experiment", "other")
KEYS = ("category", "order", "name", "description")


def load_texts():
    texts = {k: v for k, v in json.loads(TEXTS.read_text(encoding="utf-8")).items() if not k.startswith("_")}
    errors = []
    for sid, t in texts.items():
        if t.get("category") not in CATEGORIES:
            errors.append(f"{sid}: category {t.get('category')!r}")
        if not isinstance(t.get("order"), int):
            errors.append(f"{sid}: order {t.get('order')!r} (a whole number: the place inside its group)")
        for key in ("name", "description"):
            missing = [lang for lang in LANGS if not str((t.get(key) or {}).get(lang) or "").strip()]
            if missing:
                errors.append(f"{sid}: {key} without {', '.join(missing)}")
    if errors:
        sys.exit("store-texts.json:\n  " + "\n  ".join(errors))
    return texts


def base(store_id):
    for end in ("_r2", "_r3"):
        if store_id.endswith(end):
            return store_id[: -len(end)]
    return store_id


def apply(target, texts):
    changed = []
    for key in KEYS:
        if key in texts and target.get(key) != texts[key]:
            target[key] = texts[key]
            changed.append(key)
    return changed


def dump(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("check", "addons", "store"):
        sys.exit(__doc__)
    texts = load_texts()
    if args[0] == "check":
        print(f"ok: {len(texts)} add-ons")
        return
    if args[0] == "addons":
        addons = json.loads(ADDONS.read_text(encoding="utf-8"))
        by_base = {base(sid): t for sid, t in texts.items()}
        for aid, entry in addons.items():
            if aid.startswith("_") or aid not in by_base:
                continue
            changed = apply(entry, by_base[aid])
            if changed:
                print(f"{aid}: {', '.join(changed)}")
        dump(ADDONS, addons)
        return
    if len(args) < 2:
        sys.exit(__doc__)
    path = Path(args[1])
    store = json.loads(path.read_text(encoding="utf-8"))
    comps = {c["id"]: c for c in store.get("components", [])}
    for sid in sorted(set(texts) - set(comps)):
        print(f"{sid}: not in the store, skipped")
    for sid, t in texts.items():
        if sid in comps:
            changed = apply(comps[sid], t)
            if changed:
                print(f"{sid}: {', '.join(changed)}")
    if "-w" in args:
        dump(path, store)
        print(f"written: {path} - sign it now")


if __name__ == "__main__":
    main()
