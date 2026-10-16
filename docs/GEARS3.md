# Gears of War 3: target notes

Working notes for the first title. All values below come from the retail `default.xex`
(`x360tool xex-info`; full dump in [games/gow3/notes/xex-info.txt](../games/gow3/notes/xex-info.txt)).

| Item | Value |
|---|---|
| Engine | Unreal Engine 3 (`GearGame-Xbox360-Shipping.pe`) |
| Title ID / media ID | `4D5308AB` / `02EEE060`, version 0.0.0.7, disc 1 of 1 |
| Source used | Extracted disc folder (2537 files, 7.3 GB) in a 7z, not an ISO |
| XEX | AES-encrypted (retail key), **basic** compression (not LZX), base `0x82000000`, entry `0x82CE8EB0` (`_xstart`) |
| Image | 21.7 MiB: 13.3 MiB code, 5.7 MiB data, 2.7 MiB read-only |
| Functions in `.pdata` | 41,369 (leaf functions without a frame are not listed there) |
| XDK | 2.0.20500 (D3D9LTCG, XAUDIO2, XMP, XHV2, XONLINE…); compiler C1/C2 16.0.11886 |
| Imports | `xboxkrnl.exe`: 163 functions + 13 variables; `xam.xex`: 117 functions ([list](../games/gow3/notes/imported_functions.txt)) |
| Middleware in its own sections | Bink video (`BINK*`), RAD (`RAD*`) |
| Target version | Base disc executable; no Title Update |

## Sections
See [games/gow3/notes/sections.txt](../games/gow3/notes/sections.txt). Code lives in `.text`
(`0x822A0000`–`0x82FC91CC`), `BINK` and `RADCODE`.

## Recompiler configuration (done)
- **Register save/restore helpers**: each byte pattern from the XenonRecomp README occurs exactly
  once in `.text`. The addresses are in [config.toml](../games/gow3/config.toml).
- **setjmp/longjmp**: no setjmp-shaped function found (scan for a function storing r14–r31 and f14–f31
  through r3). Probably unused. The only candidate, `0x82CCC3F0`, calls `RtlUnwind` and looks like C++
  exception unwinding; still to confirm in Ghidra.
- **Jump tables**: the stock XenonAnalyse finds **0**. This compiler schedules switch sequences
  differently (e.g. `lis, rlwinm, addi, lwzx`), pads them with `ori r0,r0,0` and places absolute tables
  inline right after the `bctr`. The AnyX360 fork adds a dataflow detector. It finds **108** jump tables
  (4 of them unguarded switches with inline tables). The remaining 19 table-driven `bctr` sites were
  inspected and are indirect calls, not switches: a 256-entry bytecode dispatcher at `0x822DC8xx` and
  struct-of-function-pointer calls.
- **Function boundaries**: leaf functions containing a switch are not in `.pdata`, and the stock
  analyser stopped at their `bctr` (215 "switch case outside function" errors). Fixed in the fork.

## Instruction coverage (done for the game code)
The first run reported 5,190 unimplemented instructions of ~45 kinds that Sonic Unleashed never used:
`bdzf`, update-form loads/stores (`stfsu`, `lfsu`, `sthu`, `lhzu`…), 16-bit vector ops (`vsrah`, `vslh`,
`vsubshs`, `vcmpgtsh`…), saturating packs, `vsel128`, `eqv`, `subfze`, `addme`, `addc`, `vcfpuxws128`,
`blrl`, and float16_2 `vpkd3d128`/`vupkd3d128`. In addition `vcmpbfp128` (271 sites) emitted a
debug trap. All are implemented in the fork.

**Remaining:** 36 `mulchw.` decodes, all in the `BINK` section: an opcode-4 encoding the disassembler
reads as a PPC405 instruction. Bink is better replaced by a native decoder anyway (it only plays
`.bik` movies), so that code should never run.

## Why UE3 helps
- **Game logic is UnrealScript bytecode** inside the packages, run by the engine's VM. Only native
  engine code is recompiled; script behaviour comes for free once the VM runs.
- UE3's structure (RHI, audio device, file manager, threading) is widely documented, so engine
  subsystems can be recognised from their strings and call patterns.

## Known challenges
- **Threading**: the engine uses the Xenon hardware threads for rendering, audio, streaming and task
  graphs. Synchronisation primitives in the HLE have to be exactly right.
- **Rendering**: deferred lighting, heavy eDRAM resolves, post-processing. We replace the Xenon RHI
  with D3D12 so resolves become ordinary render-target copies.
- **Streaming**: seamless level streaming from disc. The VFS has to be fast and fully asynchronous.
- **Online**: Xbox Live for the 360 is gone. The scope is campaign, local split-screen and Horde
  offline.

## Reproducing (from the 7z in `Raw/`)
```bat
build.bat
"C:\Program Files\7-Zip\7z.exe" x "Raw\Gears Of War 3.7z" -oextracted\gow3
set T=build\tools\x360tool\Release\x360tool.exe
%T% xex-info    "extracted\gow3\Gears Of War 3\AP7kB\default.xex"
%T% xex-extract "extracted\gow3\Gears Of War 3\AP7kB\default.xex" extracted\gow3\basefile.bin
cd games\gow3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe "..\..\extracted\gow3\Gears Of War 3\AP7kB\default.xex" switch_tables.toml
mkdir recomp
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat
```
`recomp/` must be empty before regenerating: XenonRecomp does not delete output files from an earlier
run whose function split was different, which leads to duplicate symbols at link time.

## Status (2026-10-16)
- Boots through the Epic/Microsoft logos (Bink), title screen, brightness setup and every menu.
- **Campaign works**: New campaign → difficulty → Act I Prologue "Troubled Past" (`SP_Nightmare_*`):
  in-engine cinematics with subtitles and voices, then gameplay.
- **Private Horde works**: Horde → Private → Start Game → map selection (START) → character setup
  (START) → Checkout loads and plays (movement, aiming, firing, HUD).
- **Audio**: XMA decoded by Xenia's XmaDecoder (FFmpeg fork), output through XAudio2.
- Shaders and pipelines are cached in `<user>/cache/shaders`, so later runs start faster.
- Performance overlay (top-left, **F1** toggles): guest FPS and frame time, the process's VRAM use
  (DXGI budget), host RAM (working set and commit) and guest memory in use.

### How the campaign starts (for reference)
The front end calls `GearEngine.SetCheckpoint` (the chapter's start checkpoint) and travels to
`GearGame_P?game=GearGameContent.GearGameSP?chapter=N?listen`. `GearEngine::LoadMap` then reads the
chapter's data (`MapName`, e.g. `sp_nightmare_p` for chapter 0) and streams its levels into
`GearGame_P` — but only if the base `LoadMap` returned no error. `?listen` makes the campaign a listen
server for co-op, so a failing socket layer ("Error initializing network layer.") silently left the
player in the empty `GearGame_P`. Sockets are emulated locally for this reason (`xam_net.cpp`).

### Known issues
- The Horde pre-wave counter stays at 0:30 (it may wait for the command post to be built; unverified).
- An intermittent crash while the async loader creates objects (corrupt link in the object hash
  chains, `sub_8230F098`); seen loading Horde maps, not reproducible on demand.

### Analysis notes
- `GNatives` 0x833CDFD8 (`EX_VirtualFunction` 0x822CD110, `EX_FinalFunction` 0x822CD180).
- `FFrame`: `Object` +0x14, `Code` +0x18. `UObject`: `Outer` +0x28, `Name` +0x2C, `Class` +0x34.
- `GWorld` 0x83429828; `GearEngine` checkpoint fields: pending action +1864, `CurrentCheckpoint` +1880.
- Name entries are packed `[be32 index << 1][be32 hash next][text]` from 0x40130000.

## Debugging switches
All opt-in through environment variables read by the runtime:

| Variable | Effect |
|---|---|
| `ANYX360_INPUT` | Scripted pad 1 input: `"25:START,35:A,170:START/0.3"` (`seconds:button[/hold]`; buttons START BACK A B X Y UP DOWN LEFT RIGHT LB RB LT RT LUP LDOWN LLEFT LRIGHT) |
| `ANYX360_CAPTURE_DIR` | Saves the guest frame as BMP every 5 s |
| `ANYX360_DUMP_AT` | Thread dumps at the given seconds (`"95,100"`) |
| `ANYX360_MEMSCAN` | With `ANYX360_DUMP_AT`: logs guest memory holding that text (ASCII or UTF-16BE) |
| `ANYX360_TRACE` | Logs the exports a thread calls after a time: `"11:75"` (`FFFF` = all threads) |
| `ANYX360_HOOK` | Logs indirect calls to guest functions: `"8252A040,..."` |
| `ANYX360_LOG_FILES` | Logs file opens, writes and info changes |
| `ANYX360_ZPD` | Occlusion query mode: `fake`, `fast`, `fast-alt`, `strict` |
| `ANYX360_XENIA_LOG_LEVEL` | Xenia's log level |
| `ANYX360_UE3_TRACE` | UnrealScript call trace: `"<seconds>[,filter]"` (first call of each `Class => Function`, or every call matching the filter) plus logging hooks on map, checkpoint and package loading |
| `ANYX360_MEMDUMP` | With `ANYX360_DUMP_AT`: dumps guest memory, `"addr,length"` in hex |
| `ANYX360_MEMSCAN=0x...` | Searches a big-endian 32-bit value instead of text |
| `ANYX360_WATCH_WRITE` | Hardware write watchpoint on a guest address (logs the writing function) |
| `ANYX360_NO_SOCKETS` | Disables the local socket layer and the network address |

The runtime also accepts `--user <folder>` to run with a clean profile and saves.
