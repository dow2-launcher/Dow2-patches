# UI rule: no drawing over the game

**Everything an add-on shows goes into the game's own interface** — its Scaleform (GFx) HUD and menu movies, their
own text fields, buttons and tooltips — **never a picture of ours drawn over the frame through the video driver**
(Direct3D: hooking `EndScene`/`Present`/`Reset`, `ID3DXFont`, sprites, our own textures or fonts).

Owner's decision, 02.11.2026: "встроиться в существующие элементы интерфейса, а не рисовать поверх через видеодрайвер".

## Why

- **That is how the game itself does it.** Every HUD element of DoW2 is a Scaleform movie (`ui/movies/hud/*.gfx`) that the
  engine drives through `Invoke("Receive_SetText" / "SetVisible" / "DuplicateMovieClip" …)`. Text put there gets the
  game's font, scale, layout, colours, tooltips and z-order for free, at every resolution and UI scale, and hides
  with the rest of the HUD.
- **A hook into the Direct3D device is fragile and dangerous:** it sits between the game and the driver in the hottest
  path of every frame, races device loss / `Reset` on alt-tab and resolution change, collides with DXVK, ReShade, OBS,
  Steam / Discord overlays that hook the same vtable, and a mistake there is a crash or a black screen for the player.
- **It looks foreign:** our own font rasterizer, our boxes and positions measured per resolution never quite match the
  game, and break with every UI scale.
- **No benefit:** reading game state is separate from showing it — the add-ons only read the simulation either way.

## What happened (history)

- MatchTimer v1–v4, Cooldowns 1–2 and the LobbyStats main menu tooltip (0.9.x) drew through Direct3D
  (`EndScene` hook + `ID3DXFont` + our own copy of the game font, `common/gamefont.h`). Players had crashes around them
  (LobbyStats 0.9.x hooks racing on the same import page, the clock removed from LobbyStats in 0.9.4 after "games crashed").
- 02.11.2026 the owner rejected the approach. Rewritten the same day: MatchTimer 5 / matchtimer-6, Cooldowns 3,
  LoadingRatings 4 put their text into the game's own movies (`common/gfxui.h`, commit 3f6964e); LobbyStats moved the
  menu stats into the game's own tooltip of the name bar.
- LobbyStats 0.10.2 removed the last leftover (the dead Direct3D box code and its d3dx9 loading); `cgnumbers/`
  (an unshipped Direct3D experiment, control-group numbers above squads) and `common/gamefont.h` were deleted.

## How to do it

`common/gfxui.h`: find the HUD movie (widget + 4 = form, form + 4 = GFxMovieView), call its own `Receive_*` functions
through `Invoke` (vtable + 0x48) or `SetVariable` (vtable + 0x2C) — on the game's UI thread only (inside the engine's own
per-frame calls). Examples: `matchtimer/matchtimer.c` ("the clock": a text field created in the Menu button movie),
`cooldowns/cooldowns.c` ("the number": the button's own count clip), `lobbystats/lobbystats.c` (the slot tooltip, the
menu tooltip). If the game has no element for something, ask the owner first — a Direct3D overlay is not the fallback.
