# Baseline

`<id>.json` is one game's summary from `tools\regress\run_regress.ps1`, stored by `set_baseline.ps1`
(docs/REGRESSION.md). compare.ps1 compares new runs with it.

## The current baseline: AnyX360 0.33.0
- **Made** from three runs with the release `dist\AnyX360-0.33.0` (its `bin\` and launcher, used
  read-only) and the `.anyx` files of `anyx\`, one game at a time, every player's device `"none"`,
  System Link off. Each game is from the run where it got further and ran faster; `date` in its file
  says which: 2026-11-01 23:47 to 2026-11-02 01:05 (bo2, mw3, minecraft, halo3, reach, gh3, fm4, bo3,
  kjr), 01:02 to 01:05 (kp, a rerun) or 2026-11-02 19:17 to 20:45 (the others).
- **The machine was not quiet in any of them, so these numbers are a lower bound.** RTX 5060 Laptop
  GPU, 16 threads, 15.6 GB RAM, a 60 Hz display. Other agents ran 1 to 4 copies of other games beside
  each game and compiled most of the time (`machine_before`: free memory down to 1.1 GB). A second
  run was started on an idle machine for this reason, and the other agents resumed within a minute.
  Make a new baseline when the machine is truly idle.
- **Steadiness:** the probes were steady (Mortal Kombat's menu p99 18.5 ms, Gears of War 3's 18.1 ms;
  "machine unsteady" is above 300 ms) and no log has the warning about a display without vblanks.
- **Run to run**, on this loaded machine, the same game moved a lot: Forza's race 35.6 and 22.2
  frames/s, Black Ops III's menu 48.8 and 34.4, Modern Warfare 3's briefing 59.5 and 51.2. So a
  difference of that size between a run and this baseline means little unless the machine state is
  comparable: run again before calling it a regression or an improvement.

## Results

Frames/s and p99 are each phase's medians of the 5-second samples; phases are seconds from the
start (routes.json). The second column is the last screenshot of the run stored, as seen. Every stored
run went through its route without crashing or exiting, with no error lines and every screenshot taken.

| Game | Last screenshot (fresh profile) | Phase: frames/s, p99 | DLC packs |
|---|---|---|---|
| gow3 | the campaign's opening cinematic | menu 60.0, 18.1 ms; play 60.0, 19.6 ms | 0 |
| mk | a Ladder fight, "Fight!" | menu 60.0, 18.5 ms; fight 60.0, 19.1 ms | 17 |
| bo2 | the first mission, in the engine | menu 59.8, 21.3 ms; play 60.0, 17.8 ms | 8 |
| mw3 | the first mission's briefing | menu 60.0, 19.0 ms; play 59.5, 19.1 ms | 26 |
| re6 | a "data saved" notice waiting for A: the route doesn't get to the prologue on this run | menu 28.4, 53.2 ms; late 27.0, 66.3 ms | 11 |
| minecraft | a new world generating | menu 119.4, 11.6 ms; play 59.6, 29.8 ms | 92 |
| halo3 | Arrival's cinematic | menu 30.0, 33.9 ms; late 30.0, 33.8 ms | 5 |
| reach | Winter Contingency's cinematic | menu 30.0, 33.8 ms; late 29.2, 52.0 ms | 5 |
| gh3 | Career's "name your band" screen | menu 60.0, 17.9 ms; song 60.0, 19.6 ms | 25 |
| dcc | the main menu | menu 60.0, 18.7 ms; play 60.0, 20.2 ms | 1 |
| d3 | the difficulty screen (Act I isn't reached: [DIABLO3.md](../../../docs/DIABLO3.md) has the measurement in play) | menu 60.0, 18.2 ms; play 60.0, 18.4 ms | 1 |
| fm4 | the first race's start | intro 20.0, 75.9 ms; race 35.6, 107.2 ms | 21 |
| bo3 | a Team Deathmatch match | menu 48.8, 81.8 ms; late 57.0, 33.2 ms | 5 |
| dc2 | the title screen | title 60.0, 21.1 ms | 29 |
| dc3 | the title screen's menu | title 60.0, 21.0 ms | 20 |
| ka | "Is anyone there?": the player isn't seen at that moment | menu 60.0, 18.6 ms | 1 |
| fnk | slicing | play 30.0, 39.0 ms | 8 |
| ks | Boxing's intro | menu 20.2, 72.1 ms | 2 |
| kjr | the title screen (Start), not the garage | garage 30.0, 34.4 ms | 2 |
| kp | a channel's tip screen | channel 30.0, 48.9 ms | 1 |

The phase names are the routes' plans, not what was on screen. Some routes stop short of gameplay on a
fresh profile: they still compare boot, menus and frame pacing from release to release.

## Known intermittent problems seen while making it
- **Kinect Party crashed in 2 of its 3 runs**, 7-8 s in, both with the machine at its heaviest (3 or
  more other games): `sub_82B42050+0xA34`, thread 11, a guest read at 00000001, the same return
  address (`82B4215C`) both times. Not investigated. The stored run is the one that didn't crash, so
  a compare run can show this as new when it isn't. 0.32.0's baseline run didn't crash.
- **Resident Evil 6** had no window for its screenshots at 100 to 195 s in one of its two runs, while
  still presenting at 30 frames/s. Not explained; the stored run has all five.

## Notes
- **The Kinect games' rows ran with nobody in front of the sensor** (dc2, dc3, ka, fnk, ks, kjr, kp):
  until 2026-11-02 the battery wrote their scripts without a final line end, and the scripted source
  reads whole lines, so no action ran. Their "where the route gets" column describes what the games
  show on their own. Make a new baseline for them with the next release.
- Doritos Crash Course gets a version 2 avatar pack from `ks.anyx` (`avatar_pack_from` in routes.json;
  the battery starts games without the launcher, which would otherwise put the pack in place).
- Window titles that differ from the game's name are in routes.json; Minecraft renames its window
  ("Minecraft: Xbox 360 Edition", then "Minecraft") 10 s in.
