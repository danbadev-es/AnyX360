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
| Target version | 0.0.4.2: the base game patched with its title update (since 0.17.0), with its DLC |

The disc also carries the console's avatar asset pack (`AvatarAssetPack`, a PIRS package of title
`FFFE07DF` holding `AvatarAssetPack.toc`), a system update and the game's avatar awards.

## The title update (since 0.17.0)
The archive holds the game's title update as a root file, `TU_16L6269_0000008000000.0000000000102`:
an STFS "LIVE" package of content type 0x000B0000, "Kinect SportsTitle Update", 4.2 MB. It holds
`default.xexp` (a delta patch from 0.0.0.2 to 0.0.4.2) and 36 files the game reads from
`update:\`: bundles, the bundle lookup, localised text and the avatar asset inventory.

The update is a rebuild with a later XDK, 2.0.20353 (the disc: 2.0.11775): every SDK library
changed, NUI and XAVATAR included, and the image grew to 20.12 MiB (entry `0x82A70A60`). It has 23
imports the disc didn't (SHA-256/384/512, `NtWriteFileGather`, `XamShowNuiMarketplaceUI` and
others); four aren't in the runtime (`KeInitializeMutant`, `KeReleaseMutant`,
`XamGetActiveDashAppInfo`, `XamShowGoldUpgradeUI`) and weren't called on the routes below, where
the only unimplemented call was `NetDll_WSARecvFrom` (harmless offline).

- **Recompiled from the update.** `config.toml` names the base, the patch and the patched
  executable; the register helpers and setjmp/longjmp (`82FBD980`/`82FBDC50`, the base's pair matched
  by instruction shape) are the patched image's. XenonAnalyse finds 337 jump tables, 4 unresolved.
- **The Kinect library is NUI 2.0.20353**, close to Fruit Ninja Kinect's 2.0.20500, not the disc's
  2.0.11775: most entry points changed code, so they couldn't simply be shifted. Each was found by
  instruction shape (immediates and branch targets masked): the update's function of the same shape
  as the disc's (when it didn't change), else Fruit Ninja Kinect's function of the same shape
  (initialization, skeleton tracking, image streams, the XG texture functions), else the call at
  the same place in the game's own callers (the camera settings, the identity requests, the
  elevation). The leaf functions (camera settings, the troubleshooter, which calls
  `XamShowNuiTroubleshooterUI`) are the same instructions. The table is in
  [games/ks/src/nui_bindings.cpp](../games/ks/src/nui_bindings.cpp) and
  [notes/tu_addresses.md](../games/ks/notes/tu_addresses.md). The image formats now come from the
  library's tables, as Fruit Ninja Kinect's (the plain depth image is type 3 here, 4 on the disc).
- **Avatars**: the update's XAVATAR (2.0.20353) builds avatars from the same asset pack; its
  skeleton builder (`82AD7AE8`, the disc's `82A84A00`) was found through its caller.
- **At start** the game patches the copy's `default.xex` (cached), mounts the update as `update:\`
  and reads its bundles from there. Without it, it ends at once saying where the update goes; the
  launcher installs it from the archive (or the `TU_` file alone).

The addresses in the sections below are the disc's (0.0.0.2) unless said otherwise.

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
- **Clothes and faces** (unreleased): the body loses the triangles under the clothes worn (the
  pack's hiding templates; before, its skin showed through the trousers), the faces take their
  chin, nose and ears, the eyes, eyebrows and mouth show whole, and hair fits under hats
  ([AVATARS.md](AVATARS.md#shape-overrides-hiding-templates-and-face-shapes)).
- **Not done**: the system's avatar animations
  (`XamAvatarLoadAnimation`, which the game imports; it has played without them so far).
- **Scripted route** (synthetic source): `3:enter;5:walk_out;27:arm_right_10_80;35:lower;
  40:arm_right_10_80;48:lower`, then `55:arm_right_84_-24` picks "Player" (`94_-25` the guest on
  the left). In the stadium the hand at `arm_right_75_-22` is over "Play"; the sport menu's
  Bowling is at `95_-22`, "Accept Players" at `74_-22`, the how-to screen's Skip at `37_4`; a raised
  hand (`10_80`) starts the game, `52_-36` reaches the right-hand ball rack.

## Status with the title update (2026-10-28)
Prepared by the launcher from the archive (the copy, the two DLC packs and the update) and played
on the build recompiled from 0.0.4.2 with the scripted player:
- **Menus**: the logos, the title screen, "Elegir jugador principal" with the avatars (the update's
  avatar library), the guest's warning, the stadium, "Jugar".
- **The update's main menu**: Competición, Desafío calórico (the calorie challenge the update
  added), Prueba principal, Minijuegos; "¿Qué deporte te apetece probar?" with the six sports;
  "¿Quién va a jugar?" (one player against the AI); Football loads ("Equipos de hoy").
- The game reads its bundles from `update:\` (46 reads). Started without the update, it ends at once
  with the message.
- **Scripted route** (the menus moved with the update): `3:enter;5:walk_out;27:arm_right_10_80;
  35:lower;40:arm_right_10_80;48:lower;55:arm_right_90_-30` (the left guest; `79_-31` "Sí" on its
  warning), then `75_-22` Jugar, `95_-22` a sport (Football in the second row), `74_-22` against the
  AI, `37_4` skips the how-to screen.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `4D5308C9`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\4D5308C9\00007000\<file>" extracted\ks\game
build\tools\x360tool\release\x360tool.exe stfs-extract "<package>\TU_16L6269_0000008000000.0000000000102" extracted\ks\update
build\tools\x360tool\release\x360tool.exe xex-patch extracted\ks\game\default.xex extracted\ks\update\default.xexp extracted\ks\update\default_0.0.4.2.xex
cd games\ks
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\ks\update\default_0.0.4.2.xex switch_tables.toml
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```

XenonRecomp writes into `out_directory_path` from `config.toml` (`recomp4`), and that folder must
exist and be empty before it runs. A developer build needs the update's files in
`%LOCALAPPDATA%\AnyX360\KinectSports\content\000B0000\TU_16L6269_0000008000000.0000000000102\`.
