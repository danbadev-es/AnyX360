# Kinect Sports: target notes

These are working notes for the sixth title: Kinect Sports (Rare, 2010). Every value below comes
from its `default.xex`, read with `x360tool xex-info`. The full dump is in
[games/ks/notes/xex-info.txt](../games/ks/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Rare (Microsoft Game Studios, 2010) |
| Title ID / media ID | `4D5308C9` / `655F2429`, version 0.0.0.2, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`), two DLC packs and a Title Update |
| XEX | AES-encrypted with the retail key, LZX compression, base `0x82000000`, entry `0x82A1ED98` |
| Image | 20 MiB: 15 MiB code (`.text`), 1.3 MiB data, 1.9 MiB read-only |
| XDK | 2.0.11775 (the Kinect launch); links NUI and NUIHNDL 2.0.11775, XAVATAR, XAudio2, XHV2 |
| Imports | `xboxkrnl.exe`: 189 functions; `xam.xex`: 150 functions ([list](../games/ks/notes/imports.txt)) |
| Target version | The base game; no Title Update |

The disc also carries the console's avatar asset pack (`AvatarAssetPack`, a PIRS package of title
`FFFE07DF` holding `AvatarAssetPack.toc`), a system update and the game's avatar awards.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`82F6C150`, `82F6C420`, the CRT's):
  [config.toml](../games/ks/config.toml).
- **Jump tables**: 321 from the dataflow detector, 9 of them dispatched on the target address;
  the 4 left are calls through tables of function pointers.
- **Instructions**: `stvebx`, `vadduhs`, `vandc`, `vavgsw`, `vminsw`, `vsl`, `vsrb`, `vsro`,
  `vsubuwm` and `vsubuws`, added to the fork.

## Kinect
The NUI library is the same 2.0.11775 build as Kinect Adventures', linked elsewhere
(`829C0000`-`829E0000`). Each entry point the game calls was matched to Kinect Adventures' function
by its instructions with addresses and offsets masked; the bindings
([games/ks/src/nui_bindings.cpp](../games/ks/src/nui_bindings.cpp)) are Kinect Adventures', at
these addresses. The pure code around it (NUIHNDL's hand tracking and the game's gestures) stays
recompiled.

The **pause** comes from the console's Kinect pause notification (`XN_SYS_NUIPAUSE`, `0006001A`,
sent when a player holds the Kinect Guide gesture): its handler (`82528ED0`) queues the game's
pause event. The system UI notification (`9`) only sets a "system UI active" flag (`832659D4`).
The pause menu has a Kinect Guide entry (`XamShowNuiGuideUI`). Until 0.13.3 the runtime sent only
the system UI notification, and the gesture did nothing here. Every Kinect game listens for the
pause notification; Dance Central 3 answers it by showing the Kinect Guide.

## Bring-up
- **LZX decompression in the kernel** (`LDI*`): XMemDecompress and the avatar library call it. Its
  create function takes one argument more than the cabinet version (a size before the two
  pointers).
- **The GPU's ring is set up twice** (the game creates its Direct3D device again): the command
  processor now restarts both pointers; before, it ran past the new commands and the screen stayed
  black.
- **Direct3D CPU callbacks**: the GPU raises an interrupt for processor 1 and waits until the
  handler clears that processor's bit; the interrupt now runs as that processor.
- **Saving**: the game compares twice its save's size with the low 32 bits of the free space; the
  hard drive no longer reports an exact multiple of 4 GiB.
- **Avatars**: the game creates its avatar manager only if the avatar system starts, and then uses
  it regardless, so for this title it starts (`GameDescriptor::avatar_system`). The avatar library
  unpacks its own data (a placeholder body and its skeleton) through LDI. The avatars' parts come
  from the system (`XamAvatarGetAssets`), which AnyX360 builds from the disc's avatar asset pack
  (`GameDescriptor::avatar_skeleton`, [AVATARS.md](AVATARS.md)). `XamAvatarInitialize` takes the
  title's coordinate system as its second argument; read from the first, every part was mirrored
  and drawn inside out (the body over the clothes).
- **Pacing**: one vblank per frame: 60 FPS.

## Status (2026-10-20)
- **Menus**: logos, the intro video, the title screen ("raise your hand"), standing in the purple
  area, the player menu (Player, Guest, Sign In) and the save device question, with the hand
  cursor.
- **Avatars**: the players and the game's guests are Xbox Avatars made from the avatar asset pack
  on this disc ([AVATARS.md](AVATARS.md)): the guests keep the looks the game gives them (their
  clothes, hair, skin and faces); the player gets the default avatar. Their skeletons are shaped
  to each avatar's height and weight by the skeleton builder linked into the game (`82A84A00`).
  The crowds are the game's own.
- **Sports**: picking the player leads to the stadium, "Play", the sport menu and "Who is
  playing?". Bowling loads (today's competitors with the player's avatar, the how-to screen),
  starts when the hand is raised, and the avatar follows the body: it reaches for the ball rack
  and bowls. Only Bowling's start was tried with the scripted player; the other sports, whole
  games and two players have not been tried.
- **Not done**: the avatars' face shapes (chin, nose, ears) and the pack's hiding templates (the
  body under tight clothes can show through); the system's avatar animations
  (`XamAvatarLoadAnimation`, which the game imports; it has played without them so far).
- **Scripted route** (synthetic source): `3:enter;5:walk_out;27:arm_right_10_80;35:lower;
  40:arm_right_10_80;48:lower`, then `55:arm_right_84_-24` picks "Player" (`94_-25` the guest on
  the left). In the stadium the hand at `arm_right_75_-22` is over "Play"; the sport menu's
  Bowling is at `95_-22`, "Accept Players" at `74_-22`, the how-to screen's Skip at `37_4`; a raised
  hand (`10_80`) starts the game, `52_-36` reaches the right-hand ball rack.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D5308C9`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4D5308C9\00007000\<file>" extracted\ks\game
cd games\ks
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\ks\game\default.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs.
