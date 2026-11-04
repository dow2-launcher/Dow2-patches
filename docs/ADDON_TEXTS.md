# Add-on texts for players

What a player reads in the launcher: the add-on's **name**, its **description** and, when a version goes out, a **news** item.
All three are for players, not for us (siritos 04.11.2026: "человеческим языком"). The technical story stays in CHANGELOG.md,
README and the commits.

## Name

- What the player gets, in 2-6 words: "Match timer", "Keep the match when Steam drops", "Reverse".
- No DLL names, no versions, no "EXPERIMENT:" / "(test)" - the group on the Add-ons tab says that (category below).
- A condition only when it decides whether to turn it on: "All games through our server (if Relic lags)".

## Description

- 1-2 short sentences, about 150 characters, never more than 250.
- First what changes for the player, then - if needed - when to turn it on or what to press.
- Plain words. Never: UDP/TCP, ports, DLL, ini, hooks, error codes, function names, "the game's own orders",
  internal names (relay, forwarding node, MMFind). "Our nearest server" instead of "the relay node".
- Say it once. No "nothing changes for the other players" unless the player would really wonder about it.
- The same meaning in all five languages (en, ru, es, fr, de); short es/fr/de are fine.

## Category (the group on the Add-ons tab)

| category | for |
|---|---|
| `network` | our servers, the automatch, staying connected |
| `interface` | what the game shows: timers, ratings, sounds, the translation |
| `gameplay` | how units act on orders |
| `fixes` | fixes of the game or Elite, crash records |
| `experiment` | being tested, may change or go away; anything that fits no other group |

`order` is the place inside the group: a whole number, smaller = higher (10, 20, 30 ... leaves room for a new one between).
Both live in the add-on's own entry (addons.json, docs/store-texts.json, a catalog-entry.json) and go to the store with
it; the launcher (1.4.8+) places add-ons only by them. Moving an add-on = changing these two fields, not the launcher.

## News (every version of the set)

Each version has two logs:

- `CHANGELOG.md` - for us: commits, addresses, what was tested. Technical is fine there.
- `changelog/<X.Y.Z>/en.md` and `ru.md` - for the players. The GitHub Release of an X.Y.0 is made from en.md, and the
  launcher shows it in its news ("Add-ons X.Y.Z"; ru.md for Russian players). CI fails without both files.

How to write the player notes:

- A list, one line per change: `- <add-on name as the launcher shows it>: <what changed for the player>.`
  "- Match timer: no second clock after alt-tab." - not "MatchTimer: the fallback under Menu fired on restore".
- New add-ons first, with `New:`; tests with `Test:`. Fixes say what the player saw before ("the number went away").
- Nothing about commits, branches, addresses, DLL or ini names, store ids or versions of single DLLs.
- Only what reached the players (or reaches them with this version); work in branches waits for its release.

## Where the texts live

- `docs/store-texts.json` - every add-on of the store: category, name, description. The one place to edit them.
- `scripts/store_texts.py check` - all five languages there, a known category.
- `scripts/store_texts.py addons` - copies them into addons.json (its add-ons), so a deploy keeps them.
- `scripts/store_texts.py store <launcher.json> [-w]` - the hand-released add-ons too, straight into a store file
  (shows the changes; `-w` writes, then sign as usual).
