# The regression battery

`tools\regress\` plays every game for a few minutes from its `.anyx` backup, the way a player
would get it, and compares the frame rate, the frame-time spikes, the screenshots, the error lines
and the downloadable content with a stored baseline. Run it on every release against the previous
one's baseline, and after any shared runtime change.

## Running it

```
powershell -ExecutionPolicy Bypass -File tools\regress\run_regress.ps1 [-Games gow3,mk|all] ^
  [-Release dist\AnyX360-X] [-Out <folder>] [-Sandbox C:\axreg]
powershell -ExecutionPolicy Bypass -File tools\regress\compare.ps1 -Run <folder>
```

- **run_regress.ps1** takes one game at a time (all of them by default, about 70 minutes plus the
  imports):
  1. A sandbox `C:\axreg\<id>`: a release-like `root` (7-Zip, the keys) and its own `local`
     (LOCALAPPDATA) with the terms accepted (`-TermsHash`, default computed from
     `build\launcher\launcher_terms.h`).
  2. `anyx\<id>.anyx` imported by the release's launcher (`AnyX360.exe --root <root> --archive
     <file> --quit-when-done --result <file>`), as "Add game" does; the DLC packs and title
     updates installed are counted.
  3. The sandbox player's `settings.toml`: windowed, **every player's device `"none"`** (a pad on
     this machine would otherwise drive a sandbox game; `ANYX360_INPUT` connects the scripted
     players by itself), System Link off (other agents' copies on the network can't pull it into a
     party). The release's shader cache is copied in, as the launcher seeds it.
  4. The game from the release's `bin\` with its route from `tools\regress\routes.json`:
     `ANYX360_INPUT` (pads), `ANYX360_INPUT_CLOCK`, `ANYX360_NUI_SCRIPT` (the scripted Kinect
     player: never the webcam).
  5. While it runs, the log's 5-second samples (`frames:` swaps/s, p99 and max of the swap
     interval; `cpu:` cores; `gpu:` ms per frame) are timed from the start, and screenshots are taken
     at the route's times of the window titled exactly `AnyX360 - <title>` among the game's own
     processes (never a title substring: a browser tab can name the game).
  6. The game's processes are stopped by PID, and `<Out>\<id>\summary.json` is written: each
     phase's median frame rate and p99, the samples, the error lines (numbers masked), the
     screenshots and how lit they are, the DLC count, the exit, and the machine's state before and
     after (CPU load, free memory, seconds since the last keyboard or mouse input, other game
     processes running, the busiest processes). The sandbox is deleted; only the report stays.
- **compare.ps1** writes `<Out>\report.md` and `report.html` and flags: a phase's median frame rate
  10% lower, its median p99 50% and 15 ms higher, a missing or black screenshot, new error lines, a
  crash, a game that exited by itself, a different DLC count. Exit code 1 when anything is flagged.
- **set_baseline.ps1 -Run <folder>** stores a run as `tools\regress\baseline\<id>.json` (no
  screenshots: they are game images; their times and brightness are kept).

## The machine's state
A baseline is only as good as the machine was quiet. On 2026-11-01 between 03 and 06 h the machine
paused every game for about 430 ms once a second, on a display that returned no vblanks (the log says
`display vblank: the display returns no vblanks`; [ARCHITECTURE.md](ARCHITECTURE.md#pauses-of-about-04-s-a-display-that-gives-no-vblanks)).
Numbers taken with that warning are invalid. Mortal Kombat's and Gears of War 3's menus are the probes: with a p99 above 300 ms there the
run is marked **machine unsteady** (`steady: false`), and compare.ps1 turns its performance flags into
warnings. The machine's state is in every summary: check `other_games` (other agents' copies) and
`busy_processes` before trusting a slower result, and run again when the machine is idle.

## Routes
Every run starts from a fresh import: a new profile with no saves, so the first-run screens come
(the brightness, the safe area, the storage notices). The routes come from each game's doc
("Routes") and earlier test scripts; times are seconds from the first input poll, screenshots and
phases are seconds from the start. They are short on purpose: the title, the menus and, where a
fixed sequence of presses gets there from a new profile, a little gameplay.

| Game | Route | Phases |
|---|---|---|
| gow3 | START through the intro, A through the first-run screens and Campaign (route not re-run on the title update 0.0.6.7; its baseline still has 0 DLC) | menu, play |
| mk | START at the title, Fight > Ladder > Scorpion > Novice, attacks | menu (probe), fight |
| bo2 | the campaign route up to the first mission | menu, play |
| mw3 | the campaign route (content warning, first mission) | menu, play |
| re6 | the new-profile notices, START at the title, brightness | menu, late |
| minecraft | Play Game, offline, a new world, walk and dig | menu, play |
| halo3 | A, A from the main menu (solo campaign, Normal), the look prompts | menu, late |
| reach | A (Armory), START, A, A (solo campaign, Normal) | menu, late |
| gh3 | to the main menu, then DOWN, DOWN, A... (frames clock): on a new profile it ends in Career's band name | menu, song |
| dcc | A through the intro, B (continue without saving), the main menu | menu, play |
| d3 | the legal screens, then A presses (frames clock): it ends in the menus | menu, play |
| fm4 | A at the title, the brightness, the introduction, RT in the first race | intro, race |
| bo3 | the logos, the first-run screens, Multiplayer > Start match | menu, late |
| dc2, dc3 | the scripted dancer enters: the title screen | title |
| ka | the player enters: the menus | menu |
| fnk | the player enters and slices | play |
| ks | the player enters, walks out and in, points at the menu | menu |
| kjr | Start and the autosave notice with the hand: the garage | garage |
| kp | the player enters: the first channel, then arm moves | channel |

**Modes' routes.** A route with `"game": "<id>"` (e.g. `"kjr_battle"`) plays that game's mode: it
takes the game's `game.json`, `.anyx`, shader cache and folder, and has its own sandbox, report and
baseline under the route's name. `-Games all` runs the games' own routes only; `-Games modes` runs
the modes' routes, or name them one by one. They are longer (a fresh profile goes through the
first-run screens and tutorials first) and are run when a change touches those modes.

| Mode route | Game | What it plays |
|---|---|---|
| ka_reflex | ka | the first start, Free Play, Reflex Ridge's first level: the avatar on the platform, the start, the jumps |

What each route really reached on 0.33.0 (kjr stops at the title screen, ka at "Is anyone there?", re6 at a notice) is in `tools\regress\baseline\README.md`; a route that
stops short of gameplay still compares boot, menus and pacing. Doritos Crash Course gets the version 2
avatar pack from `ks.anyx` (`"avatar_pack_from"`, extracted with the main checkout's x360tool), as
the launcher would install it: with Reach's older pack it crashes 10 s in.

Window titles that differ from the game's name: `AnyX360 - COD: Black Ops II`,
`AnyX360 - Modern Warfare® 3`, `AnyX360 - COD: Black Ops III`, `AnyX360 - Guitar Hero III`, `AnyX360 - Diablo III: RoS`, `AnyX360 - Mortal Kombat`, `AnyX360 - RESIDENT EVIL 6`, `AnyX360 - Minecraft`. A route with
`"title": null` takes the first `AnyX360 - ` window of the game's processes and reports it.

## Rules for runs
- One game at a time; only processes the battery started are stopped (by PID).
- Sandboxes take the game's size (up to 13 GB): each is deleted after its run.
- The release folder is used read-only (`AnyX360.exe` and `bin\`), and so is `anyx\`.
- The user may be playing: never send input to other windows; screenshots use PrintWindow.

## Baseline
See `tools\regress\baseline\README.md` for when and how the stored baseline was made and what it
says about each game.
