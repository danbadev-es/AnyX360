# The Guide and the dashboard

**Status:** implemented for 0.22.0. The third stage of the console experience (profiles,
achievements, then the Guide and the dashboard, then the avatar editor), on
[PROFILES.md](PROFILES.md) and [ACHIEVEMENTS.md](ACHIEVEMENTS.md).

## The idea
As on the console: the Xbox button opens a menu over the game, which pauses it; from there the
player goes back to the dashboard, and from the dashboard into another game, all in one window. The
launcher gets a **console mode** for that: a dashboard driven by the controller first, in its
window (maximized when it starts) or borderless over the whole screen. The older layout stays as
**desktop mode**.

Every game still runs in a process of its own (what Modern Warfare 3's and Black Ops II's second
executables, and a crash that takes only the game down, need), so "one window" is what the player
sees: the game's window opens hidden at exactly the dashboard's place, shows itself with its first
frame, and the dashboard hides behind it; when the game ends, the dashboard is back first.

## The Guide (the Xbox button)
`runtime/src/gpu/guide_menu.cpp`. What opens it:

- **The Guide button** of an Xbox controller. Windows' `XInputGetState` doesn't report it; the input
  thread (`input/device_thread.cpp`) reads the controllers with `XInputGetStateEx`, ordinal 100 of
  `xinput1_4.dll` (or `xinput1_3.dll`), loaded at run time as Xenia's driver does, and falls back to
  the plain function. The bit (`0x0400`, `input::kGuideButton`) reaches the system's UIs
  (`input::GetUiPad`) and never the game (`input::GetPad` clears it).
- **Back and Start held together for a second**, for a controller whose Guide button Windows keeps
  (the Game Bar, below) or that has none.
- **Home** on the keyboard (the keyboard's player; player 1 when no slot has it).
- Scripted runs: `GUIDE` in `ANYX360_INPUT` (`30:GUIDE`), or `ANYX360_SETTINGS_TEST="30:guide"` (as
  Home).

The same press closes it. It doesn't open over a system UI the title opened itself (its sign-in or
achievements screen) or over the F2 panel.

The menu, driven by every controller (the d-pad or the stick, A or Start to choose, B back) and the
keyboard (arrows, Enter, Esc), in the game's language (English or Spanish):

| Entry | What it does |
|---|---|
| **Resume** | Closes the Guide. |
| **Achievements** | This game's achievements for the profile on the controller that chose it (the list of [ACHIEVEMENTS.md](ACHIEVEMENTS.md)). A controller without a profile signed in is told to sign in. |
| **Profiles** | Players 1-4: who is signed in on each slot (a guest's says "Guest"). A free slot (2-4) offers **Choose a profile** (the sign-in picker for one player) and **Guest**, a guest of player 1's profile on that slot ([PROFILES.md](PROFILES.md#guests)); a signed-in one (2-4) signs out. Player 1 stays signed in. |
| **Settings** | The F2 panel's pages, over the game; closing the panel (Esc) comes back to the Guide. |
| **Xbox Home** | Asks first ("progress you haven't saved is lost"), then saves the settings and the time played and ends the game with `kExitToDashboard` (0x00DB0001): the dashboard comes back at once. |

A short sound plays when it opens (F2 > Display > Overlay > Guide sound turns it off). Its header
has the player's gamertag, the game and the time, as the console's.

### The pause
As on the console, the game is paused behind it. The system UIs share one state
(`kernel::SetSystemUiOpen`, `hle/xam.cpp`): the Kinect Guide gesture, the sign-in picker, the
achievements list and the Guide are bits of it.

- The title gets `XN_SYS_UIVISIBLE` (1) when the first opens and (0) when the last closes; what the
  Guide opens over itself (its achievements list, the picker) doesn't close and reopen it.
  `XamIsUIActive` is true meanwhile.
- In a Kinect title (`[kinect] enabled`) the Guide is first the pause gesture of player 1
  (`XN_SYS_NUIPAUSE`), as the Kinect Guide: Kinect Sports pauses on that alone, Dance Central 3
  answers it with its Kinect Guide.
- While one drawn over the game is open, the game reads neutral controllers
  (`input::SetSystemUiOpen`); the keyboard and the mouse cursor are the menu's.
- A player signed in or out while it is open is told with `XN_SYS_SIGNINCHANGED` alone (the system
  UI stays open).

`tests/hle/test_guide.cpp` checks these notifications as a title's listener reads them.

### Notifications
The achievement toast's queue (`gpu/achievement_toast.cpp`) also shows the system's notifications,
with a number or a letter in the circle and no chime: a controller connected or disconnected
("Controller 2 connected") and a player signed in or out ("Player 2 - Ana signed in"). F2 > Display >
Overlay > System notifications (`system_notifications`) turns them off.

### Windows' Game Bar and the Xbox button
Windows opens its Game Bar with the Xbox button and keeps the press from other programs. To use the
Guide with it:

- **Windows 11:** Settings > Gaming > Game Bar > turn off **"Allow your controller to open Game
  Bar"**.
- **Windows 10:** Settings > Gaming > Xbox Game Bar > turn off **"Open Xbox Game Bar using this
  button on a controller"**.
- **Steam** (when it runs) takes the button for its own overlay with Steam Input: Steam > Settings >
  Controller > **"Guide Button Focuses Steam"** off.

Or hold **Back and Start** for a second instead: it works with any controller.

## Console mode (the dashboard)
`tools/launcher/ui/app.cpp` (`DrawConsole` and the pages), `ui/home_page.cpp` (Home), `ui/main.cpp`
(the window).

The launcher starts in console mode (unless desktop mode was chosen), **maximized**: a normal
window with its title bar, the taskbar in view. Settings > Display switches to **full screen**,
borderless over the whole monitor (and back to the maximized window).

### Home
The first screen, in the spirit of the Xbox 360's dashboards (AnyX360's own logo and colours, not
Microsoft's):

- **The background**: the selected game's art (`backgrounds\<slug>.jpg`, [backgrounds/README.md](../backgrounds/README.md)),
  slightly blurred so the dashboard stays in front, darker at the top and towards the floor; a game
  without art shows its cover enlarged and blurred. It cross-fades when the selection changes. The
  blurred picture is made once per game and window size (a small copy, stretched smoothly).
- **The top bar**: at the left the AnyX360 mark and name; at the right the gamercard (round gamerpic,
  gamertag, the gamerscore with its "G": it opens Profiles), the controller (bright when one is
  connected, with how many if more than one) and the time.
- **The row**, standing on a dark mirror floor that reflects it: the profile's **avatar** at the left,
  full body, its arms down (drawn by the avatar viewer into a picture once, again only when the
  avatar or its size changes); then the games, **newest played first**: the selected one's box art
  large, in a green neon frame, with its title, time played and gamerscore (earned of the game's)
  under it; the one before it smaller, translucent and blurred, half behind the avatar; the next
  ones at the usual size, dimmer the further they are. Moving glides the row (0.3 s).
- **The panel**: frosted glass at the bottom, **My games** (the Games page), **Settings** and **Add
  games** (the Add page: `.anyx` backups, archives, discs, folders, downloadable content); the
  controller's hints at its left (A Select, Y More), LB RB at its right (or what the last action
  ended with).

Controller and keyboard: left and right go through the games; **left of the first game the avatar
takes the focus** (a green ring at its feet; **A** opens the Avatar page, or "Who's playing?" with
nobody signed in), right from it back to the first game; down goes to the panel, up to the
gamercard. **A** or **Start** plays the selected game, **Y** opens its ⋯ menu. The mouse clicks a
game to select it and again to play it; the wheel or a trackpad's sideways swipe moves the row.
With no games yet, "Add a game" stands where the selected game would.

### The pages
Changed with **LB** and **RB** (Tab and Shift+Tab), the panel, or the row of pages the other pages
have under their header (the AnyX360 mark, the gamercard and the time):

| Page | What it has |
|---|---|
| **Home** | Above. |
| **Games** | Every game in the library as box art, then **Add a game**. **A** or **Start** plays one, **Y** opens its ⋯ menu (cover, logs, .anyx backup, remove). |
| **Achievements** | A row per game: unlocked of all, the gamerscore earned of the game's; **A** opens its list. |
| **Profiles** | Each profile (its gamerpic, gamertag, gamerscore; the one playing marked): **A** plays as it. **Create profile**, and **Manage profiles** (rename, delete, its avatar and gamerpic, the language). |
| **Avatar** | The avatar editor for the profile playing (0.23.0, [AVATARS.md](AVATARS.md)): the avatar at the left (the right stick turns it and zooms to the face), the rows at the right (up and down choose one, left and right change it at once), **Y** a random avatar, **Start** or **Save** saves it in the profile, **Gamerpic…** its picture. Leaving the page (B, LB, RB, the Guide button) with changes asks first. |
| **Settings** | The display (in the window or full screen), the launcher's language, **Desktop mode**, the terms. |
| **Add a game** | From a file or a folder, as desktop mode's **Add game**; a file dropped on the window too. |

**B** goes back to Home; the Guide button on the dashboard too. The mouse and the keyboard work as
in desktop mode. The header of desktop mode has **Console mode**; the choice is remembered in
`%LOCALAPPDATA%\AnyX360\launcher_mode.txt` (`desktop`, `console` or `console fullscreen`).
`AnyX360.exe --console` or `--desktop` starts in one this time only. Screenshots (`--screenshot`)
have `--state console-home`, `console-home-avatar` (the avatar focused, also `home-avatar-focused`)
and `console-home-panel`; `--select <id>` chooses Home's game.

## One window
`tools/launcher/core/launch.cpp` (`StartGame`, `StartFollowed`), `runtime/src/gpu/xenia_gpu.cpp`,
`runtime/include/x360/dashboard.h`.

### Starting a game
The launcher starts it with (`GameCommandLine`):

```
<Game>.exe --game <folder> --key <key> --dashboard <launcher process ID> [--window x,y,w,h] [--fullscreen]
```

- `--window`: the dashboard's place on the screen, in pixels: its whole window as it looks (title bar
  included, so no desktop shows above the borderless game; maximized, within the work area), or its
  client area when it is borderless. The game's window is created
  there borderless (a CBT hook changes its style and rectangle as Xenia creates it, then the
  runtime sets them again in case Xenia resized it for the monitor's DPI). The game renders at its
  usual resolution and scales to it. The saved fullscreen setting doesn't apply.
- `--fullscreen` with it: borderless over the whole monitor of that rectangle (Xenia's own
  fullscreen).
- `--dashboard`: the window is hidden (DWM-cloaked, from a hook on its `WM_SHOWWINDOW`, before
  anything of it reaches the screen; cloaking it as it is created doesn't hold) and shown, in front,
  when the first guest frame is presented (or after 30 seconds without one). In desktop mode the launcher passes
  only this one: the game opens its own window as before, but the launcher still hides only once
  the game is on the screen.

The launcher makes three named, auto-reset events for it, `Local\AnyX360.Dashboard.<pid>.ready`,
`.leaving` and `.shown`:

| Event | Who sets it | What follows |
|---|---|---|
| `ready` | the game, with its first frame | the launcher hides |
| `leaving` | the game, about to end on its own: Xbox Home, its window closed, the title's own exit, a switch to its other executable | the launcher shows the dashboard (painted and composed), then sets `shown` |
| `shown` | the launcher | the game (which waited up to 1.5 s) ends |

So nothing else shows between the game and the dashboard. A crash doesn't warn: the dashboard comes
back when the process has ended, with the error, as before.

### Following the game: the job object
`StartFollowed` starts the game suspended, puts it in a job object with a completion port, and
resumes it; everything it starts is in the job too. It follows the processes started from the game
executable's folder (a process another one starts there, as `gpu::ReplaceProcess` does for Modern
Warfare 3's and Black Ops II's multiplayer, the settings panel's restart): the game has ended when
the last of them has, and its exit code is that one's. Others (Windows' console host, the Kinect
pose bridge) don't keep it waiting. `tests/launcher/test_launcher_core.cpp` checks a process that
replaces itself with another (followed to the second's end and its code) and a helper from another
folder (not waited for).

`ReplaceProcess` passes `--dashboard`, `--window` and `--fullscreen` on to the next process, and
sets `leaving` before it ends: the dashboard covers the switch, with "Starting <game>…", until the
next executable's first frame.

### When it ends
| Exit code | The launcher |
|---|---|
| 0 (the window closed, the title's own exit) | the dashboard |
| `kExitToDashboard` (Xbox Home) | the dashboard, at once |
| anything else (a crash, an error) | the dashboard with the error and the game's log, as before |

Before 0.22.0 the launcher closed when a game ended normally; now it stays.

## Not done
- **The game's window inside the dashboard's** (a child window): it would need changes to Xenia's
  window and to the input hook; the hidden window at the same place looks the same.
- **Quick Resume** (a game kept suspended while another runs): the Xbox 360 didn't, and it would
  cost about 3 GB of memory per game.
- **Low battery** notifications for wireless controllers.
