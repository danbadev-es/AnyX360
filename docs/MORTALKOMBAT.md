# Mortal Kombat Komplete Edition: target notes

These are working notes for the fourteenth title, NetherRealm's fighting game (2011; the Komplete
Edition, 2012). Every value below comes from its `default.xex`, read with `x360tool xex-info`; the
full dumps are in [games/mk/notes/](../games/mk/notes/).

| Item | Value |
|---|---|
| Developer | NetherRealm Studios (Warner Bros. Interactive Entertainment, 2012) |
| Engine | Unreal Engine 3, NetherRealm's (`MK9Game-Retail.exe`), as Gears of War 3 |
| Title ID / media ID | `575207FD` / `6579DAFA`, version 0.0.0.8, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 1,263 files, 6.76 GiB) and 17 downloadable content packages. No title update in the copy |
| XEX | `default.xex`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x8236E580` |
| Image | 21.38 MiB: 16.12 MiB code (`.text`), 1.31 MiB data, 3.94 MiB read-only |
| XDK | 2.0.20353; links D3D9, D3DX9, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, XMP, XHV2, XAVATAR, XAUD, XAUDIO2, XAPOBA, XMCORE, LIBCPMT and VCOMP (OpenMP); compiler 16.0.11886 |
| Imports | `xboxkrnl.exe` and `xam.xex`: 363 and 242 import records ([list](../games/mk/notes/imports.txt)) |

The game's data are UE3 packages (`Asset\*.xxx`: characters `CHAR_*`, stages `BGND_*`,
fatalities, intros, the interface), `Config`, `Localization` and WMV videos (`Movies`: the logos and
the story mode's cinematics, played by the XDK's XMedia, linked into the executable).

## The copy
The Games on Demand package (`575207FD\00007000`) holds the Komplete Edition's disc, which already
carries the four downloadable fighters (`CHAR_Skarlet`, `CHAR_Kenshi`, `CHAR_Rain`,
`CHAR_FreddyKrueger`, with their fatalities, voices and interface) and the Klassic costumes, as
well as the bosses (`CHAR_Goro`, `CHAR_Kintaro`, `CHAR_ShaoKahn`). Next to it, 17 content packages
of type 2 (`00000002`):

| Package | What it holds |
|---|---|
| Skarlet, Kenshi, Rain, Freddy Krueger | Each fighter's packages, texts in six languages and an `.ini` that unlocks it (`[Player.DownloadContentUnlocked]`) |
| Kombat Pass | `OnlinePass__Xe-MK9Game.ini` (`bValidOnlinePass=TRUE`) |
| Kombat Pass + All Character Costumes | The costume packs' packages and unlocks |
| Mileena, Sub-Zero, Jade, Scorpion, Reptile, Ermac, Kitana Classic Costume | An `.ini` alone (the costume's unlock; its package is on the disc) |
| Goro, Kintaro, Shao Kahn, "GORO TEST" | Not official: packages that make the bosses playable (`IsDLC=goro`, content ID 6001); "GORO TEST" is an English-only copy of "Goro" |

The 33 avatar items (`00009000`) aren't used.

## Recompiler configuration
[config.toml](../games/mk/config.toml), [switch_tables.toml](../games/mk/switch_tables.toml):
- **Register save/restore helpers** from find_helpers.py; **setjmp/longjmp** `82FE1340`/`82FE1020`,
  checked by hand (the CRT's, as Modern Warfare 3's, Black Ops II's and Forza Motorsport 4's).
- **Jump tables**: 389, from the dataflow detector; 24 table-driven `bctr` left.
- **One function given by hand** (`82CF05B0`, an HRESULT map): a leaf function without a `.pdata`
  entry whose switch counts down CTR with `bdzf`; its last conditional branch targets the next
  instruction, which the analyser took for its end (as Black Ops II's and Resident Evil 6's).
- **The other "ERROR" branches** in the recompiler's output (about 75) are in functions it makes of
  the 8 bytes of exception data that precede each function with a handler (and of the rest of that
  function after them): nothing calls them, and the real function, from `.pdata`, is complete.
- No instruction was missing.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `575207FD`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\575207FD\00007000\<file>" extracted\mk\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\mk\game\default.xex extracted\mk\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\mk\default.bin
cd games\mk
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\mk\game\default.xex switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release mk
```

XenonRecomp writes into `out_directory_path` (`recomp`), which must exist and be empty before it
runs. To play a developer build with the downloadable content, the packages go, extracted, in
`<user>\content\00000002\<package file name>\` (the launcher does it).
