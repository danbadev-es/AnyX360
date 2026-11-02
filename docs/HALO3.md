# Halo 3: target notes

These are working notes for Halo 3, Bungie's game of 2007 and the first title on its own engine
(not Unreal, not IW). Every value below comes from its executables, read with `x360tool
xex-info`; the dumps are in [games/halo3/notes/](../games/halo3/notes/).

| Item | Value |
|---|---|
| Developer | Bungie (Microsoft Game Studios, 2007) |
| Title ID / media ID | `4D5307E6` / `3DFC2730`, version 0.0.0.5, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 166 files, 5.68 GiB), its title update and 5 map packs |
| XEX | `default.xex` (`halo3_cache_release.exe`): AES-encrypted with the retail key, basic compression, base `0x82000000` |
| Image | 19.12 MiB with the title update: 6.69 MiB code (`.text`, `BINK`), 11.69 MiB data, 0.6 MiB read-only |
| Functions | 34,002 |
| XDK | **2.0.6132** (the oldest of the ports); links XAPILIB, D3D9, D3DX9, XGRAPHC, XNET, XONLINE, XAUD, X3DAUD, XHV, XMP, LIBCPMT |
| Imports | `xboxkrnl.exe`: 180 functions; `xam.xex`: 110 ([list](../games/halo3/notes/imports.txt)) |
| Target version | Patched with the title update (`TU_16L61V6_000000K000000.00000000000o5`): 0.0.0.5 to **0.0.3.5** |
| Title DLLs | `WaveShell-Xbox.dll`, `WavesLibDLL.dll`, `waves\L360.dll`, `waves\Q10.dll`: Waves audio processing, recompiled into the executable |

The game's data are Bungie's cache files (`maps\*.map`: `mainmenu`, `shared`, `campaign`, the
campaign's levels `005_intro` to `130_epilogue` and the multiplayer maps), fonts and Bink videos.

## The copy
`raw\H3-DLC-TU-GOD.rar` (not encrypted) holds:

- `4D5307E6\00007000\B5659FF16461F3362243` and its data files: the Games on Demand package;
- `TU_16L61V6_000000K000000.00000000000o5` at the root: the title update as the console's cache
  keeps it (an STFS package of type `000B0000`, 0.9 MiB: `default.xexp` and a `.dllp` for each of
  the four DLLs);
- `4D5307E6\00000002`: 5 map packs (Heroic, Legendary, Cold Storage, Mythic, Mythic II);
- `4D5307E6\00009000`: avatar items, not used.

The launcher installs the title update and the map packs.

## Recompiler configuration
- **From the title update**: [config.toml](../games/halo3/config.toml) names the base, the patch and
  the patched executable. Output: `games/halo3/recomp6`.
- **Register save/restore helpers** from find_helpers.py on the patched image.
- **setjmp/longjmp** `82709CB0`/`82709870`, checked by hand: this old CRT keeps CR, LR and a flag
  at words 304-312 of the jmp_buf; the other longjmp candidates are ordinary functions.
- **Jump tables placed after the `bctr`** (XenonRecomp `anyx360-halo`): this compiler puts each
  table right behind its dispatch. The fixed patterns found 626, but for tables without a bounds
  check they took a `cmplwi` from a function before (another register, another count), and with
  no table location the recompiler ended each function at its `bctr`: 6,840 cases were "outside
  the function". The patterns now need the check on the table's own register within the
  function and emit `table`/`table_size`; unchecked tables after their `bctr` are dispatched on
  the target address (4); a case that branches to another function's start is a tail call.
- **The "ERROR" branches left** (about 170): exception funclets and `__finally` blocks that branch
  back into their parent, functions made of the 8 bytes of exception data before a function with
  a handler, and cases that point at the padding after a function (never taken).

## Bring-up
- **I/O completion ports**: the game reads its maps asynchronously from an `ASYNC_IO` thread
  (`NtCreateIoCompletion`, files associated with `FileCompletionInformation`,
  `NtRemoveIoCompletion`). The runtime has them now, for every title.
- **The cache partitions** by their own names, `cache0:` and `cache1:`, as Xenia's `mount_cache`:
  the game keeps `preferences.dat` there and looks for map copies (`cache000.map`...).
- **Map signatures**: `XeCryptBnQwNeRsaPubCrypt` (RSA with the public key, Xenia's code) and
  `XeCryptBnDwLePkcs1Verify`; without them the game reported a disc read error.
- **The Waves DLLs**: at start the game loads `WaveShell-Xbox.dll`, `WavesLibDLL.dll`,
  `waves\L360.dll` and `waves\Q10.dll` with `XexLoadImage`, checks each one's version (export 1),
  creates the L360 limiter and the Q10 equalizer on its mix and loads their presets from its own
  resources (`mix_l360`...); any failure is a disc read error. They are recompiled as title
  modules (`config_waveshell.toml`, `config_waveslib.toml`, `config_l360.toml`,
  `config_q10.toml`, as Forza Motorsport 4's), with three changes:
  - the plug-ins import from `WavesLibDLL.dll`: the recompiler makes those thunks branch through
    the import's variable record, and the loader fills it with the other DLL's export;
  - `XexGetModuleSection` answers for the DLL it is given (the plug-ins describe themselves in
    resources, `xinf1000`, `xcli1000`...);
  - the function-table slots of `WavesLibDLL.dll` (at `0x8A000000`) fall at `0x93200000`, and
    Xenia's 0x90000000 heap is a view of the 0x80000000 one: they overwrote the title's resources
    at `0x833xxxxx` (the presets) and the start of its own table.
    `GameDescriptor::separate_xex_4k_heap` gives that heap memory of its own (Xenia option
    `xex_4k_heap_mirrors_64k_heap`, fork branch `anyx360-halo`).
  The title update's `.dllp` patches change only the DLLs' headers.
- **XenonRecomp hang**: analysing `WaveShell-Xbox.dll` never ended (a conditional branch before
  the function's start, in code analysed from its middle); fixed in the recompiler.
- **Fibers**: XAPI's `SwitchToFiber` (`825B5FE0`) is a branch to stack-switching assembly
  (`825B8320`) the recompiler can't follow. The map loader (`822C51F0`) switches to a fiber and
  polls until it is done, so the first loading screen never ended: the runtime's host fibers run
  them ([games/halo3/src/fibers.cpp](../games/halo3/src/fibers.cpp): `ConvertThreadToFiber`
  `825B5DC0`, `ConvertFiberToThread` `825B5E50`, `CreateFiber` `825B5EA0`, `DeleteFiber`
  `825B5F80`), as for Forza Motorsport 4 and Mortal Kombat.
- **The music notice**: `XMPGetPlaybackController` (XMP message `0x0007001B`) didn't answer, and
  the game found the user's music playing: "music notice" before every campaign. It answers that
  the title has the playback now.
- **Players 2-4**: `XamUserGetSigninInfo` and `XamUserGetXUID` answered `ERROR_NO_SUCH_USER` for a
  slot nobody is signed in to; XAPI's wrapper checks the HRESULT's sign bit, so that read as
  success, and the lobby had four players, three without a controller: "the Xbox 360 controller
  is disconnected" and "Error starting the game". They return the HRESULT now, as XAM does.

## Fixes after 0.31.0 (2026-11-01)
- **Speed: made for 30 FPS.** As Halo: Reach ([HALOREACH.md](HALOREACH.md#fixes-after-0310-2026-11-01)):
  the game presents every other vblank of the console's 60 Hz, and its menus and cinematics
  advance a step per frame. With the runtime's default 120 Hz vblank, Arrival ran twice as fast:
  Sierra 117's first prompt came about 80 s after Normal, and Johnson's cigar about 50 s in. At
  60 Hz they come at about 175 s and 120 s. The game now always runs at 30 FPS
  (`vblanks_per_frame = 2`, `fixed_fps = 30`).
- **Tessellated triangle strips** with an index buffer are drawn now (Reach's faces needed them).
  Halo 3 didn't log such failed draws in these runs.

## Fixes after 0.32.0
- **The hard drive: the utility drive mounts.** Theater and campaign co-op over System Link said
  "one or more players don't have an Xbox 360 hard drive", offline too. The game takes a hard drive
  to be there when it could mount the hard drive's cache partitions at start (`sub_821A8448`):
  `XMountUtilityDriveEx` (`825982F8`), `XFlushUtilityDrive` (`82597AD0`), then a write to
  `cache0:\test.dat`; only then does it set its "using utility drive" flag (`828C3F05`). Its own
  copy of the XDK's `XMountUtilityDriveEx` reads partition 0's header (2 KiB in, magic "Josh"),
  formats each cache partition through its raw volume (an XTAF header with the cluster size it
  asks for, 64 KiB) and mounts it with a file system driver of its own (`IoCreateDevice`, a
  device extension, the title's own I/O routines). The runtime's volumes are host folders: the
  format's writes failed, and that driver can't run. The cache partitions are mounted as
  `cache0:` and `cache1:` from the start, so the game's two calls are replaced
  ([games/halo3/src/utility_drive.cpp](../games/halo3/src/utility_drive.cpp)) and answer success
  (`x360::kernel::MountUtilityDrive`). Theater opens ("no film selected"), a campaign lobby over
  System Link says "Ready: this party is open to System Link" and starts Arrival. With the drive
  the game keeps its campaign checkpoints in `cache1:` (`savegame0.bin`, `savegame1.bin`, 8 MiB
  each) and caches its font package in `cache0:\maps\cached_fonts`, as on the console: the
  user folder's `cache` and `cache1` folders.

## Status (2026-11-01)
- **Boot**: the loading screen, the main menu (version 1.2: Start solo game, Campaign,
  Matchmaking, Custom games, Forge, Theater), at 60 FPS with stutters (below). Preferences are
  kept in `cache0:` and the profile's settings in its title-specific settings.
- **Campaign**: Start solo game, difficulty, the lobby ("Ready: this party is local to this Xbox
  360"), Arrival (the opening cinematic, subtitled) and **Sierra 117**: the look calibration
  prompts, then gameplay with the HUD (assault rifle, radar). The pause menu offers the last
  checkpoint (one is saved at the mission's start) and Save and quit; after Save and quit the
  lobby's mission is Sierra 117: the campaign's progress is kept.
- **Split screen**: two players in **co-op on Sierra 117** (Master Chief and the Arbiter, each
  with their HUD), and four players in a **custom game** (Slayer on Last Resort), from the lobby
  with each player signed in (`[players] sign_in = "at_start"` and a controller each, or the
  sign-in picker).
- **Not working yet**: the map packs. The game enumerates only saved games (content type 1) at
  start and lists the 11 maps of the disc (Forge's and Custom games' map list); it never asks for
  its downloadable content (`XamContentCreateEnumerator` type 2 is never called), so Heroic,
  Legendary, Cold Storage, Mythic and Mythic II aren't offered. What is known (2026-11-01):
  - The only call of `XamContentCreateEnumerator` is the content catalogue's enumeration task
    (`sub_8230D2D8`, vtable `82042ADC`, "XContentCreateEnumerator"): content type 2 on every
    device when the task's kind (task+20) is 1, else type 1 on the player's device. Each
    player's catalogue (four, built by `sub_82293CD8`) embeds the task at +616, and the only code
    that starts it (`sub_82296438`, from the storage check `sub_8230BB08`) sets the kind to 0. No
    other code was found that sets kind 1 or uses the task's vtable.
  - The game sends no XAM content messages (`XMsgStartIORequest` only to XMP) and never calls
    `XamContentCreateEx` for a package of type 2. The packages' maps would be read through a
    content root (`%s:\%s`, `sub_821F5AB8`) once mounted.
  - Not the cause: the hard drive (the utility drive mounts since 0.33.0), the notifications
    (`XN_LIVE_CONTENT_INSTALLED` at start was tried: its handler only refreshes the four
    file-share/film browsers at `831A87F0`, stride 0x1050), the device data (an HDD, type 1).
  - Next: find what sets the catalogue task's kind to 1 (a probe on writes to catalogue+636, or
    the title update's own DLC check: the update's `default.xexp` changed the catalogue), and
    whether an Xbox LIVE sign-in state gates it.
- **Theater** (2026-11-02): every custom game and Forge session is recorded, and Recent videos lists
  it ("Slayer"); Start video plays it (the timeline, the recorded player's view, 0:38 of 1:05 seen).
  Two things stood in the way, both shared with Reach. While recording, the game tries to delete the
  film's file (`cache1:\autosave\asq_<map>_<id>.temp`): the recorder opened it sharing nothing, so on
  the console that fails; here every open shared everything, so the film was deleted a moment after
  it was created. And at its end it is renamed to `.film` (`FileRenameInformation`), which the runtime
  didn't have. Both are `GameDescriptor::file_share_access`, on for the two Halos.
- **Forge** (2026-11-02): up to four players in split screen (Last Resort), the Monitor (D-pad up),
  the object menu (X), placing objects (A). **Not working**: saving. Save as new asks for a name (the
  on-screen keyboard answers at once with what the game proposes, `keyboard_ui_accepts`), then
  "Failed to save: the variant couldn't be saved to the saved games device", with no system call at
  all: the game refuses from its own state. It looks like the map packs' question below, its content
  catalogue never getting past saved games; Reach saves.
- **Split screen, campaign**: for three or more players on one console the game says only two can
  play split screen in campaign, as on the console.
- **Further campaign missions** need a person: a new profile's mission list offers Arrival and
  Sierra 117.
- **Not tried yet**: a whole custom game to its score limit, saving a film to the film library.
- **System Link** (with `[network] enabled`): the lobby's Network > System Link hosts and finds
  games; a second copy joins and a custom game is played ([SYSTEMLINK.md](SYSTEMLINK.md)). The
  observer only connects to secure-style addresses (first byte 0, `823B7420`). A campaign lobby
  over System Link is ready and starts the mission (the hard drive check passes since 0.33.0);
  co-op with a second copy joining is to be tried.
- **Xbox LIVE** (Matchmaking) is gone.

## Known issues
- **Smoothness, to be checked by a player**: on the bring-up day the test machine showed a pause
  of about 420-440 ms roughly once a second in every game it ran: Halo 3, Halo: Reach, Gears of
  War 3, Black Ops II, and Mortal Kombat built from 0.29.0's code, which players had run at a
  steady 60 FPS. So the pauses came from that machine's state, not from the ports; the games'
  smoothness still has to be judged on a normal session.

## Routes (scripted input)
With an existing user folder and the game at its main menu (about 50 s in), using
`ANYX360_INPUT_LIVE` to append presses as screens come (the menu remembers the last entry chosen):

- **A film**: on Custom games `A`, `A` (Slayer on Last Resort); after a while `START`, `A`, `UP`, `A`
  ends it; `B`; on Theater `A`, `UP`, `A`: Recent videos; `A`, `A`; `DOWN` to Start video, `A`.
- **Forge**: on Forge `A`, `A`; D-pad up turns into the Monitor, `X` opens the objects, `A` places.

- **Campaign**: `A` (Start solo game), `A` (difficulty Normal): the lobby starts Arrival; after the
  cinematic, `RUP/1.0`, `RDOWN/1.0` for the look prompts, then Sierra 117 loads (about 60 s).
  `START`, `DOWN`, `DOWN`, `A`, `UP`, `A` saves and quits.
- **Co-op and split screen**: `settings.toml` with `[players] sign_in = "at_start"`, `p1 = {
  device = "pad" }`, `p2 = { device = "pad2" }` (and `p3`/`p4`), scripted presses on each player
  (`9999:BACK@1`...) so the controllers are connected; in the lobby Change mode > Campaign (or
  Custom games), then Start game.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D5307E6`):

```
build.bat
set T=build\tools\x360tool\release\x360tool.exe
%T% god-extract "<package>\4D5307E6\00007000\B5659FF16461F3362243" extracted\halo3\game
%T% stfs-extract "<package>\TU_16L61V6_000000K000000.00000000000o5" extracted\halo3\update
%T% xex-patch extracted\halo3\game\default.xex extracted\halo3\update\default.xexp extracted\halo3\update\default_0.0.3.5.xex
%T% xex-extract extracted\halo3\update\default_0.0.3.5.xex extracted\halo3\default_patched.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\halo3\default_patched.bin
cd games\halo3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\halo3\update\default_0.0.3.5.xex switch_tables.toml
mkdir recomp6
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
```

and for each DLL (`waveshell`, `waveslib`, `l360`, `q10`) the same with its `config_<name>.toml`
(XenonAnalyse on `..\..\extracted\halo3\game\<file>` into `switch_tables_<name>.toml`, output
`recomp_<name>`), then `build.bat release halo3`. To play a development build, the update's files
go in `<user>\content\000B0000\TU_16L61V6_000000K000000.00000000000o5\` and each map pack,
extracted, in `<user>\content\00000002\<package file name>\` (the launcher does both).
