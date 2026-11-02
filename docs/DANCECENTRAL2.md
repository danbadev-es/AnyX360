# Dance Central 2: target notes

These are working notes for Dance Central 2, Harmonix's Kinect dance game of 2011, the one before
Dance Central 3 ([DANCECENTRAL3.md](DANCECENTRAL3.md)): the same Milo engine and the same Kinect
library, one build earlier. Every value below comes from its executables, read with
`x360tool xex-info`; the dumps are in [games/dc2/notes/](../games/dc2/notes/).

| Item | Value |
|---|---|
| Developer | Harmonix Music Systems (Microsoft Game Studios, 2011) |
| Title ID / media ID | `373307D2` / `47AE82B4`, version 0.0.0.5, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 23 files, 2.96 GiB), the title update and 29 downloadable content packages |
| XEX | `default.xex` (`ham_xbox_h.exe`): AES-encrypted with the retail key, basic compression, base `0x82000000` |
| Image | 15.8 MiB: 9.9 MiB code (`.text`, `BINK`, `RADCODE`), 2.3 MiB data, 3.6 MiB read-only |
| Functions in `.pdata` | 47,666 |
| XDK | 2.0.20764; links NUIPRE, NUISP and NUIAUD (Kinect), XHV2, XMIC, D3D9, XAUDIO2; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe`: 185 functions and 13 variables; `xam.xex`: 165 functions ([list](../games/dc2/notes/imports.txt)) |
| Target version | Patched with the title update (`TU_0RJ61UI_000000K000000.0000000000085`): 0.0.0.5 to **0.0.1.5** |

## The copy
`raw\DC2-DLC-TU-GOD.rar` (not encrypted) holds:

- `373307D2\00007000\39C497F3B0E6D65F611F` and its 19 data files: the Games on Demand package (the
  disc: `default.xex`, `gen\main_xbox.hdr` and seven ARK archives, Kinect identity and speech data);
- `TU_0RJ61UI_000000K000000.0000000000085` at the root: the title update as the console's cache
  keeps it, an STFS "LIVE" package of type 0x000B0000 ("Dance Central 2Title Update", 1.9 MiB:
  `default.xexp`, `gen\patch_xbox.hdr` and `gen\patch_xbox_0.ark`);
- `373307D2\00000002`: 29 downloadable content packages (25 single songs, the Lady Gaga, Janet
  Jackson and Rihanna dance packs, and the Dance Central Import Pack, 201 MB); 780 MB extracted;
- `373307D2\00009000`: 10 avatar items, not used.

The launcher installs the title update and the 29 packages, as for the other games.

## Recompiler configuration
- **From the title update**: [config.toml](../games/dc2/config.toml) names the base, the patch and
  the patched executable (`x360tool xex-patch` makes the same file for XenonAnalyse). Output:
  `games/dc2/recomp2`.
- **Register save/restore helpers** from `tools/scripts/find_helpers.py` on the patched image.
- **setjmp/longjmp**: `82BD8A50` / `82BD8620`, the CRT's (checked by hand: the same code as Black
  Ops II's). Dance Central 3 had none.
- **Jump tables**: the dataflow detector finds 166; the 2 table-driven `bctr` left (`82A91B88`,
  `82A93990`) are tail calls through tables of function pointers, correct as indirect calls.
- **A missed function**: `822AD5D8` is a virtual function (a vtable entry at `820053C0`) that comes
  right after a call to `_CxxThrowException` (`826C0510`), which doesn't return: it has no `.pdata`
  entry and the analyser took it for the end of the function before. The game ended at the title
  screen on "indirect call to an address without recompiled code: 822AD5D8"; it is in `functions`
  now. Every other code address the data refers to (`build`-time scan of vtables and callbacks) has
  its function.
- The 312 `// ERROR` branches in the output: 87 go to a `blr` (they return, which is right); the
  others are exception funclets and SEH scopes of the CRT and the XDK's libraries jumping into
  their parent. None was reached in the runs.

## Kinect
The same approach as Dance Central 3: the NUI library linked into the executable is replaced at
its API boundary by the virtual sensor ([runtime/src/nui](../runtime/src/nui)), bound in
[games/dc2/src/nui_bindings.cpp](../games/dc2/src/nui_bindings.cpp).

- **Finding the entry points**: `tools/scripts/nui_recon.py` over `0x826DB000`-`0x82760000` lists
  what the game calls ([nui_entry_points.md](../games/dc2/notes/nui_entry_points.md)). Each was
  matched to Dance Central 3's by its size, the kernel functions it reaches, its arguments and its
  caller in the same Harmonix layer; the speech library's thin wrappers have the same code in the
  same order. The table is in [nui_api.md](../games/dc2/notes/nui_api.md).
- **The same structures and formats**: the image format tables (`820C5220`, `820C52B0`) have Dance
  Central 3's layout, and the title's XG texture functions (`8296F688`, `8296F840`) its code.
- Differences: Dance Central 2 doesn't move the camera itself, doesn't create the NUI's
  echo-cancelling audio, and calls fewer identity functions. Speech recognition fails to start, so
  the game turns voice off.
- **Milo's main loop** runs from an exception filter (`App::Run` `822A0520`), as in Dance Central 3:
  [milo_hooks.cpp](../games/dc2/src/milo_hooks.cpp) calls the filter.

## Songs: title keys
A song's audio (`.mogg`, version 14) is decrypted with a key the game derives through the console's
security engine: `XKeysSetKey(0, table[version])` puts a key from a table in the executable
(`82CA2D88`) in slot 0xE0 (`XeKeysSetKey`), and `XKeysAesCbc` decrypts with it (`XeKeysAesCbc`).
The table holds the keys in the console's obscured form, which the hypervisor turns back into the
real ones. The runtime takes the real values from `keys\title_keys.txt`
(`GameDescriptor::title_keys`, [keys/README.md](../keys/README.md)): the same four keys as Dance
Central 3's. Without them the game waits on its loading screen for good.

## Downloadable content
The game finds its 29 packages (the content aggregate enumerator), but opens each one with
`XamContentCreateInternal`, which it looks up by ordinal (xam `0x271`); the runtime doesn't offer it
by ordinal, so the packages aren't opened and their songs aren't listed. With it (tried on this
branch, then left out), the songs were listed (Party Rock Anthem, Promiscuous, the Dance Central
import pack's Poker Face, Pump Up the Jam...), but starting one ended on "the song data you accessed
is no longer available, or the owning profile isn't signed in to Xbox LIVE": the game checks the
packages' license further. Dance Central 3 had the same, and also crashed on the song list's
previews, so the ordinal stays out until that is understood (Dance Central 3's findings of
2026-11-01 are in [DANCECENTRAL3.md](DANCECENTRAL3.md#downloadable-content); the two games share
the code).

## Status of the modes (2026-11-02)
Played on release 0.33.0 from `dc2.anyx` (a fresh profile, the launcher's install), with two
scripted players and a scripted controller; nothing needed fixing:

- **Fitness**: the Fitness menu (session and total calories, the workouts: Easy Start, Warm-ups,
  Toned, Gym...) and a workout started: its playlist plays song after song (Last Night, then the
  next one), the session's song count goes up, and leaving through the pause menu ("Song list",
  then Yes) comes back to the Fitness menu.
  **Open: calories and time stay at 0.** The top row, "Fitness mode on", has its box filled (on) by
  default, and with it on a whole workout song was danced by the scripted player: "songs played"
  went up each time (4 in the end), but the session's and the total calories and durations stayed
  at 0, and no calorie count showed during the song. A press on the row empties the box; the box was
  filled again after the next workout. The cause isn't known (this game doesn't link the Kinect's
  FITNESS library, which Dance Central 3 does, so it counts them in its own code, not found yet): it
  needs comparing with a person on a webcam.
- **Two players**: a second scripted player who walks in at "Waiting for another player" is
  seen at once: the screen splits into the two players' sides (the first player moves to the left
  one), the second chooses their difficulty and Ready with their own right-arm swipes, and the song
  is played by both; both scores go up (34,440 each after a minute of `dance`, in a Fitness
  workout). The first time, Perform's tutorial comes first.
- **Dance Battle**: from the song list (song, "Dance Battle", each player's difficulty, a crew,
  Ready), its tutorial, then the battle: both scores, and the results with the winner's crew
  (5,000 to 3,000).
- **Break It Down**: from the song list (song, "Break It Down", difficulty, Ready), its tutorial,
  then "Full song session": the moves are taught one by one, a move not matched gets its cross. The
  scripted `dance` doesn't match the taught moves, so it doesn't move on by itself; a person
  should play it through.
- **Pause and resume**: the controller's Start pauses (Resume, Skip song, Restart song, Song
  list); **Resume works with the Kinect gestures**: the right arm out (`menu_right`) highlights
  Resume, a swipe (`swipe_left`) chooses it, and the song goes on with the scores kept. It didn't
  work before because the game was in controller mode: after a controller's press, the menus listen
  to the controller only until "Exit controller mode" (the controller's Start, shown at the bottom)
  is chosen. "Song list" asks to confirm (Yes is the upper item).

## Status (2026-10-31)
- **Boot**: logos, the intro video, the first-run "how to navigate" page, the title screen and the
  main menu (Dance, Crew Challenge, Fitness, Options, Buy new dances), at 60 FPS. Saves, settings
  and the song cache are written (`content\00000001`). Text and voices follow the system language.
- **Menus**: the scripted player's gestures (the right arm points, a swipe selects; the song list
  scrolls with the hand's height) and a controller (A, B, the D-pad; the game switches to controller
  mode).
- **Perform**: Sandstorm (Darude), Easy, played to its results: dancing (the scripted `dance`
  routine from the song's start) scored **5,424**; standing still, **0**. The post-song menu (New
  song, Photos, Break It Down, Restart song) follows.
- **Break It Down**: the post-song menu opens its move list (Dance, the moves to practise), but the
  scripted inputs didn't start it (it starts from the song list: see above).
- **Pause**: a controller's Start pauses a song (the pause menu: Resume, Break It Down, Restart
  song, Song list). The virtual sensor recognises the Kinect Guide gesture during the song and opens
  the system UI ("the Kinect guide gesture: the system UI opens"), but the song was already paused
  then, so the gesture's own pause wasn't seen. Resuming with the scripted inputs (A, Start, a swipe
  on Resume) didn't work then: the menus were in controller mode (see above: with gestures it
  works).
- **Not tried yet**: Crew Challenge (Fitness, Dance Battle and two players: see above).
- **A stale song cache**: with the downloadable songs once listed (the ordinal above), the song
  cache (`content\00000001\songcache`) and the save keep them; without the ordinal, choosing one
  ends on the "song data no longer available" message. Deleting `songcache` rebuilds it. Players
  of the release never had them listed.
- **Menus' glass panels**: drawn correctly with the default render target path; Dance Central 3
  needs the rasterizer-ordered view path for them, Dance Central 2 doesn't.
- **Not supported**: voice commands, online features.

## Routes (scripted input)
With a user folder past the first run, a controller for the menus and the scripted player for
the dancing (times from the start; `ANYX360_INPUT_LIVE` lets a driver append presses as screens
come):

```
ANYX360_NUI_SCRIPT=3:enter            (then dance/400 when the song starts)
ANYX360_INPUT=95:A,101:A,112:A/0.3,118:A/0.3,124:A/0.3,130:A/0.2,136:A/0.2,142:A/0.2,148:A/0.4
```

A at the title and the main menu (Dance), the song list (the last song played is selected; Y
changes the sort), the song, Perform, Easy, Ready, then "Play solo" on "Waiting for another
player". The first time, the title screen comes after the "how to navigate" page (a swipe) and
Perform shows its tutorial (about a minute; Continue at the bottom right, then "Got it" and Accept).

**Only gestures** (a driver appending to a live script, `ANYX360_NUI_SCRIPT=@<file>`, and
watching the screen): the menus are on the right; `menu_right` raises the right arm, the hand's
height moves the highlight (`hand_down`, `hand_up` or `hand_y_<cm>`; the highlight follows the
hand's movement, so check the screen), `swipe_left/0.8` chooses, and `menu_left` then
`swipe_right/0.8` is Back. A press on the controller puts the menus in controller mode: press
Start once to go back to gestures.

- **Two players**: `enter@1` at "Waiting for another player"; the second player's menu is on the
  right too: `menu_right@1`, `swipe_left/0.8@1` (difficulty, then Ready). Both then `lower`,
  `dance/400` and `dance/400@1`.
- **Dance Battle**: main menu Dance, a song, `hand_down` and a swipe (Dance Battle), each player's
  difficulty and Ready (the first player's arrives at "Change dancer" with the hand too low: choose
  a crew, then `hand_up` and Ready), a swipe on each tutorial page, then both dance.
- **Break It Down**: main menu Dance, a song, the third mode, a difficulty, Ready, the tutorial's
  pages, "Full song session".
- **Pause and resume**: the controller's Start, then `lower`, `menu_right` (Resume) and
  `swipe_left/0.8`.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `373307D2`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\373307D2\00007000\39C497F3B0E6D65F611F" extracted\dc2\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\TU_0RJ61UI_000000K000000.0000000000085" extracted\dc2\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\dc2\game\default.xex extracted\dc2\update\default.xexp extracted\dc2\update\default_0.0.1.5.xex
build\tools\x360tool\release\x360tool.exe xex-extract extracted\dc2\update\default_0.0.1.5.xex extracted\dc2\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\dc2\default_patched.bin
cd games\dc2
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dc2\update\default_0.0.1.5.xex switch_tables.toml
mkdir recomp2
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release dc2
```

To play a development build, the update's files go in
`<user>\content\000B0000\TU_0RJ61UI_000000K000000.0000000000085\` and each downloadable content
package, extracted (`x360tool stfs-extract`), in `<user>\content\00000002\<package file name>\`
(the launcher does both); the songs need `keys\title_keys.txt` (found next to the executable or in
a folder above it).
