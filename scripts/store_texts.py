"""Player texts of the add-ons (docs/store-texts.json) -> a launcher.json and/or addons.json.

  store_texts.py check                     docs/store-texts.json is complete (5 languages, a known category, an order number)
                                           and addons.json "_presets" is well formed (launcher 1.6.35)
  store_texts.py addons                    writes the texts into addons.json (its add-ons only)
  store_texts.py store <launcher.json>     shows what would change in that store file
  store_texts.py store <launcher.json> -w  writes it (then sign it as usual: elite-publish sign ...)

Only category, order, name, description (and danger, when set) are touched; versions, files, options stay as they are.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXTS = ROOT / "docs" / "store-texts.json"
ADDONS = ROOT / "addons.json"
LANGS = ("en", "ru", "es", "fr", "de")
CATEGORIES = ("network", "interface", "gameplay", "fixes", "performance", "experiment", "debug")
KEYS = ("category", "order", "name", "description", "danger")   # danger: launcher 1.6.29 (optional)


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


# launcher 1.6.35: the add-on presets of the Add-ons tab (addons.json "_presets" -> launcher.json "presets" by the deploy)
# never in a preset: the add-ons that change the simulation (gameplay, a "danger" text) and the speed-up / debugging groups.
# "experiment" is not refused here: the store moved some of them to network by hand (roomjoin, livereplay, replayopen) and
# this file keeps the group of the first deploy
UNSAFE_CATEGORIES = ("gameplay", "performance", "debug")
# store ids released by hand that have no entry in docs/store-texts.json
STORE_ONLY = ("elitetext_lang",)


def check_presets(texts):
    pre = json.loads(ADDONS.read_text(encoding="utf-8")).get("_presets")
    if pre is None:
        return 0
    errors = []
    if not isinstance(pre, dict) or not pre:
        sys.exit('addons.json _presets: {"<key>": {"name": {...}, "description": {...}, "ids": "defaults" | [<store ids>]}}')
    known = {base(sid): t for sid, t in texts.items()}
    for key, p in pre.items():
        if not isinstance(p, dict):
            errors.append(f"{key}: not an object"); continue
        for k in ("name", "description"):
            missing = [lang for lang in LANGS if not str((p.get(k) or {}).get(lang) or "").strip()]
            if missing:
                errors.append(f"{key}: {k} without {', '.join(missing)}")
        ids = p.get("ids")
        if ids == "defaults":
            continue
        if not isinstance(ids, list) or not all(isinstance(x, str) and x for x in ids):
            errors.append(f"{key}: ids is \"defaults\" or a list of store ids"); continue
        if len(set(map(base, ids))) != len(ids):
            errors.append(f"{key}: an id twice")
        for sid in ids:
            t = known.get(base(sid))
            if t is None and sid not in STORE_ONLY:
                errors.append(f"{key}: {sid} is no add-on of docs/store-texts.json")
            elif t is not None and t.get("category") in UNSAFE_CATEGORIES:
                errors.append(f"{key}: {sid} is {t['category']} - not for a preset")
            elif t is not None and t.get("danger"):
                errors.append(f"{key}: {sid} is marked dangerous - not for a preset")
    if errors:
        sys.exit("addons.json _presets:\n  " + "\n  ".join(errors))
    return len(pre)


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
        n = check_presets(texts)
        print(f"ok: {len(texts)} add-ons" + (f", {n} presets" if n else ""))
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
