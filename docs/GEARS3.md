# Gears of War 3: target notes

Working notes for the first title. Facts marked **TBD** are filled in during Phase 1 from the
real files (`x360tool xex-info`), not from memory.

| Item | Value |
|---|---|
| Engine | Unreal Engine 3 (Epic Games, 2011) |
| Title ID | TBD (from `xex-info`) |
| Disc layout | TBD (`iso-info`: XGD2 or XGD3) |
| XEX compression | TBD (LZX expected) |
| XDK build | TBD (static libraries list) |
| Image size / functions | TBD |
| Kernel / XAM imports | TBD |
| Target version | Base disc executable first; a Title Update may come later (it changes every address) |

## Why UE3 helps
- **Game logic is UnrealScript bytecode** inside the packages, run by the engine's VM. Only native
  engine code needs recompiling, and script behaviour comes for free once the VM runs.
- UE3's structure (RHI, audio device, file manager, threading) is widely documented, so engine
  subsystems can be recognised in the disassembly from their strings and call patterns.
- Native class and function names are registered with the engine as strings, which helps a lot with
  labelling functions in Ghidra.

## Known challenges
- **Size**: tens of thousands of functions. Recompilation time and the jump table count scale with it.
- **Threading**: the engine uses the Xenon hardware threads for rendering, audio, streaming and task
  graphs. Synchronisation primitives in the HLE have to be exactly right.
- **Rendering**: deferred lighting, heavy eDRAM resolves, MLAA-style post-processing. We replace the
  Xenon RHI with D3D12 so resolves become ordinary render-target copies.
- **Streaming**: seamless level streaming from disc. The VFS has to be fast and fully asynchronous.
- **Online**: Xbox Live for the 360 is gone. The scope is campaign, local split-screen and Horde
  offline. LAN is a stretch goal.

## Phase 1 checklist (when the ISO is in `Raw/`)
```bat
build.bat
set T=build\tools\x360tool\release\x360tool.exe
%T% iso-info    Raw\gow3.iso
%T% iso-extract Raw\gow3.iso extracted\gow3
mkdir games\gow3\notes 2>nul
%T% xex-info    extracted\gow3\default.xex          > games\gow3\notes\xex-info.txt
%T% xex-extract extracted\gow3\default.xex extracted\gow3\basefile.bin
```
Then load `basefile.bin` into Ghidra (language: PowerPC big-endian 64-bit with VMX128, base address
from `xex-info`) and start on `games/gow3/config.toml`.
