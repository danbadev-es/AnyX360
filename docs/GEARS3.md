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
