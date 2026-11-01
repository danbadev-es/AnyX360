# Baseline

`<id>.json` is one game's summary from `tools\regress\run_regress.ps1`, stored by `set_baseline.ps1`
(docs/REGRESSION.md). compare.ps1 compares new runs with it.

## The current baseline: AnyX360 0.32.0
- **Made** on 2026-11-01 from 20:43 to 22:11 with the release `dist\AnyX360-0.32.0` (its `bin\` and
  launcher, used read-only) and the `.anyx` files of `anyx\`, all 20 games, one at a time, in
  sandboxes under `C:\axreg`, every player's device `"none"`, System Link off.
- **Machine:** RTX 5060 Laptop GPU, 16 threads, 15.6 GB RAM, a 60 Hz display. **It was loaded:** two
  other agents were running their own game copies (MW3 and BO2 multiplayer, Halo: Reach, Dance
  Central 3, Gears 3) and compiling (up to 12 clang-cl processes): CPU load before each run was
  43-100% (`machine_before` in each file). The user was mostly away (idle 0-24 min).
- **Steadiness:** the probes were steady: Mortal Kombat's menu p99 18.0 ms, Gears of War 3's 18.8 ms
  (the threshold for "machine unsteady" is 300 ms). The 03-06 h stalls of the same day (~430 ms once
  a second) weren't there.
- **What it means:** frame rates measured on a loaded machine are a lower bound. Where a game is
  slower here than its usual speed (Forza's race at 46.6 frames/s, Kinect Sports' menus), check
  `machine_before.other_games` before calling a later, faster run an improvement. Make a new baseline
  on an idle machine when one is available (wave 2's performance work should start with that).

## Results

Frames/s and p99 are each phase's medians of the 5-second samples; phases are seconds from the
start (routes.json). Every game ran its whole route without crashing or exiting, with no error lines
and every screenshot taken.

| Game | Where the route gets (fresh profile) | Phase: frames/s, p99 | DLC packs |
|---|---|---|---|
| gow3 | the campaign's opening cinematic | menu 60.0, 18.8 ms; play 60.0, 19.4 ms | 0 |
| mk | a Ladder fight (Scorpion vs Stryker) | menu 60.0, 18.0 ms; fight 60.0, 19.1 ms | 17 |
| bo2 | the first mission, in the engine | menu 54.0, 33.5 ms; play 59.5, 22.2 ms | 8 |
| mw3 | the first mission's briefing (loading) | menu 60.0, 18.2 ms; play 57.2, 26.5 ms | 26 |
| re6 | the prologue's opening, in the engine | menu 30.0, 33.8 ms; late 30.0, 39.6 ms | 11 |
| minecraft | a new world generating ("Inicializando servidor") | menu 60.0, 18.4 ms; play 60.0, 17.9 ms | 92 |
| halo3 | Arrival's cinematic | menu 30.0, 33.7 ms; late 30.0, 33.7 ms | 5 |
| reach | Winter Contingency's cinematic | menu 30.0, 33.9 ms; late 28.6, 48.0 ms | 5 |
| gh3 | Career's "name your band" screen | menu 59.8, 18.3 ms; song 60.0, 18.6 ms | 25 |
| dcc | the main menu, past the no-save notice | menu 60.0, 19.2 ms; play 59.8, 25.7 ms | 1 |
| d3 | the main menu's difficulty screen | menu 60.0, 20.1 ms; play 60.0, 20.9 ms | 1 |
| fm4 | the first race, driving | intro 23.5, 62.1 ms; race 46.6, 36.4 ms | 21 |
| bo3 | a Team Deathmatch match | menu 50.2, 72.9 ms; late 60.0, 18.1 ms | 5 |
| dc2 | the title screen | title 60.0, 20.5 ms | 29 |
| dc3 | the title screen | title 60.0, 22.7 ms | 20 |
| ka | the menus | menu 60.0, 18.4 ms | 1 |
| fnk | slicing | play 30.0, 38.0 ms | 8 |
| ks | Bowling's intro video | menu 26.3, 55.8 ms | 2 |
| kjr | the garage | garage 30.0, 33.7 ms | 2 |
| kp | the first channel | channel 30.0, 41.7 ms | 1 |

The phase names are the routes' plans; the second column says what the screenshots showed. Some
routes stop short of gameplay on a fresh profile (gh3's band name, d3's menus, minecraft's world
generation, mw3's loading): they still compare boot, menus and frame pacing from release to release.
The 30 frames/s games run at their own speed (Halo 3, Reach, RE6; the Kinect games' menus and
videos).

## Notes
- Doritos Crash Course needs a version 2 avatar pack: the battery takes it from `ks.anyx` (this
  machine's own `%LOCALAPPDATA%\AnyX360\system` pack was Reach's version 1, with which Doritos
  crashes 10 s in, as the 0.32.0 changelog says; the launcher repairs it before each start, the
  battery starts games without the launcher).
- Window titles that differ from the game's name are in routes.json; Minecraft renames its window
  ("Minecraft: Xbox 360 Edition", then "Minecraft") 10 s in.
