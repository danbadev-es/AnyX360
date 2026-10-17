# Dance Central 3: target notes

Working notes for the second title, a Kinect-only dance game. All values below come from its
`default.xex` (`x360tool xex-info`; full dump in
[games/dc3/notes/xex-info.txt](../games/dc3/notes/xex-info.txt)).

| Item | Value |
|---|---|
| Engine | Harmonix Milo (`ham_xbox_h.exe`; "ham" is Dance Central's internal name) |
| Title ID / media ID | `373307D9` / `2BE86B86`, version 0.0.0.5, disc 1 of 1 |
| Source used | Extracted disc folder (2375 files, 6.2 GB): `default.xex`, `gen\main_xbox.hdr` + 10 ARK archives, Kinect identity and speech data |
| XEX | AES-encrypted with the **devkit** key (a re-packed dump), basic compression, base `0x82000000`, entry `0x827FC450` |
| Image | 16.1 MiB: 10.2 MiB code, 2.4 MiB data, 3.6 MiB read-only |
| Functions in `.pdata` | 49,865 |
| XDK | 2.0.21173, compiler C1/C2 16.0.11886; links NUI, NUIAUD, NUISP and FITNESS (Kinect), XHV2 and XMIC |
| Imports | `xboxkrnl.exe`: 185 functions + 13 variables; `xam.xex`: 154 functions ([list](../games/dc3/notes/imported_functions.txt)) |
| Middleware | Bink video (`BINK*`), RAD, libjpeg, Ogg Vorbis (song audio, decoded on the CPU) |
| Target version | Base disc executable; no Title Update |

## Sections
See [games/dc3/notes/sections.txt](../games/dc3/notes/sections.txt). Code lives in `.text`
(`0x822D0000`–`0x82CE8EFC`), `BINK` and `RADCODE`.

## Recompiler configuration
- **Executable**: XenonRecomp's loader falls back to the devkit key when the retail key doesn't give
  a PE image (fork change); x360tool and the runtime already did.
- **Register save/restore helpers**: each README pattern occurs exactly once; found with
  `tools/scripts/find_helpers.py`, addresses in [config.toml](../games/dc3/config.toml).
- **setjmp/longjmp**: no candidates. libjpeg is linked, but without its setjmp error path.
- **Jump tables**: the fork's dataflow detector finds 180; 6 table-driven `bctr` stay unresolved
  (listed at the end of [switch_tables.toml](../games/dc3/switch_tables.toml)).

## Kinect
The skeleton tracking is not a system service: the NUI library is linked into the executable and
gets raw depth images from the camera driver (`PsCamDeviceRequest`). Instead of emulating the
camera, AnyX360 replaces the library at its API boundary (`NuiInitialize`,
`NuiSkeletonGetNextFrame`, `NuiImageStream*`…) and feeds it a virtual sensor.

| Range | What |
|---|---|
| `0x823BF000`–`0x823CD000` | Harmonix's Kinect layer; its init `0x823C53A0` wraps each NUI call in a named profiling scope |
| `0x82800000`–`0x82870000` | NUI library (1782 functions); about 48 entry points are called from the game |
| `0x828151F8` | `NuiInitialize` |
| `0x8280D328` | `NuiImageStreamOpen` (color and depth) |

Most imports the runtime didn't have yet are only called from inside the NUI library (timers,
mutants, LDI decompression, `ObCreateObject`, symbolic links, Etx, Xlfs, every `XamNui*`), so they
are not needed once the library is replaced.

## Reproducing
The game folder is used where it is: `extracted\dc3\game` is a directory junction to it, and only
`default.xex` is copied to `extracted\dc3\` for the recompiler.

```
mkdir extracted\dc3
copy "<game folder>\default.xex" extracted\dc3\
mklink /J extracted\dc3\game "<game folder>"
build.bat
cd games\dc3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\dc3\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
build.bat
```

`recomp\` must be empty before XenonRecomp runs; to regenerate, use a new folder and point the
build at it with `cmake --preset windows-clang -DDC3_PPC_DIR=<folder>`.

## Bring-up notes
- **Main loop through an exception**: Milo's `App::Run` (`0x82311A68`) installs an
  unhandled-exception filter and then stores to address 0 on purpose; the filter (`0x82311A40`) runs
  the main loop. There is no guest exception dispatch, so `games/dc3/src/milo_hooks.cpp` calls the
  filter directly.
- **Switches the analyser missed**: `XGetLocale`'s result indexes a 37-entry table in Milo's startup;
  XenonAnalyse took the call's result for a constant (calls now end the dataflow) and the switch had
  become an indirect call whose cases ran as separate functions.
- **Pacing**: the title presents on every vblank, so the vblank runs at the console's 60 Hz.

## Status (2026-10-17)
Boots to the main menu ("Start the party") at 60 FPS without a sensor: `NuiInitialize` reports the
device as not connected and the menu waits for a hand. The imports still unimplemented are only
called from inside the NUI library.
