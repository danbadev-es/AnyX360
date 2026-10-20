# Call of Duty: Modern Warfare 3: target notes

These are working notes for the seventh title and the first controller game since Gears of War 3:
Call of Duty: Modern Warfare 3. Every value below comes from its `default.xex`, read with
`x360tool xex-info`. The full dump is in [games/mw3/notes/xex-info.txt](../games/mw3/notes/xex-info.txt).

| Item | Value |
|---|---|
| Developer | Infinity Ward and Sledgehammer Games (Activision, 2011); IW engine 5 |
| Title ID / media ID | `415608CB` / `20344957`, version 0.0.0.8, disc 1 of 1 |
| Source used | The **Games on Demand** package (`x360tool god-extract`: 187 files, 7.46 GiB), 26 DLC packs, 43 avatar items and a Title Update |
| XEX | `iw5sp_ship.exe`: AES-encrypted with the retail key, **basic** compression, base `0x82000000`, entry `0x82459E58` |
| Image | 35.2 MiB: 4.4 MiB code (`.text` and `BINK`), 29.5 MiB data, 1.25 MiB read-only |
| Functions | 36,494 (15,024 in `.pdata`) |
| XDK | 2.0.20500; links D3D9LTCG, XGRAPHC, XAPILIB, XNET, XONLINE, XPARTY, X3DAUDL, LIBCMT and the older XAUD and XHV 2.0.8276; compiler 16.0.11886 |
| Imports | `xboxkrnl.exe`: 160 functions and 13 variables; `xam.xex`: 98 functions ([list](../games/mw3/notes/imports.txt)) |
| Target version | The single-player executable of the base game; no Title Update |

The game folder holds two executables: `default.xex` (campaign and Special Ops) and
`default_mp.xex` (multiplayer), which the single-player one starts with `XamLoaderLaunchTitle`.
Only `default.xex` is recompiled; choosing Multiplayer ends the process. The game's data are IW
fast files (`*.ff`), Bink videos (`*.bik`) and `imagefile*.pak`/`packfile1.pak`.

The package's other content: 26 DLC packs (content type 2: the Collections, Elite drops and the
free maps, for multiplayer and Survival), 43 avatar items (content type `0x9000`) and a Title
Update (`000B0000`). None of them is used yet.

## Recompiler configuration
- **Register save/restore helpers** and **setjmp/longjmp** (`824C0AF0`, `824C1590`, the same CRT
  code as Kinect Sports'): [config.toml](../games/mw3/config.toml). The engine's error handling
  (`Com_Error`, from Quake III) leaves the frame with `longjmp`: 13 calls to `setjmp`, 46 to
  `longjmp`.
- **Jump tables**: 128 (5 from the fixed patterns, 123 from the dataflow detector). The 10
  table-driven `bctr` left are tail calls through tables of function pointers in `.data`, correct
  as indirect calls.
- **Instructions**: `vpkd3d128` and `vupkd3d128` with the 2:10:10:10 type (normals packed into a
  word), added to the fork.
- **Switches on the index register's low word**: the game clears a flag with
  `rlwinm r11,r11,0,23,21` (a mask that wraps around, which copies the word into the register's
  high half) before a switch. The bounds check only looks at the low word, but the generated
  `switch` used the whole register and jumped outside the host's jump table: a crash about five
  minutes into the campaign. The fork now switches on the low word.

## Bring-up
- **Kernel exports**: kernel semaphores (`KeInitializeSemaphore`, `KeReleaseSemaphore`),
  `KeResumeThread` (the render and worker threads are created suspended), `KeInsertQueueDpc`
  (Direct3D signals an event from a DPC; run at once on the calling thread, as in Xenia), IRQL
  and spin lock variants, `vswprintf`, `XamShowMessageBoxUI`, `XamContentGetDeviceName`,
  `XamLoaderSetLaunchData`, `XAudioGetVoiceCategoryVolumeChangeMask`, `WSAResetEvent`,
  `WSACloseEvent`.
- **I/O completion routines**: the fast file loader (the "Database" thread) reads with
  `ReadFileEx` and sleeps alertably (`SleepEx(..., TRUE)`) until the completion routine has run.
  APCs were never delivered, so the loader waited forever before the first video. Completion
  routines now run as user APCs at the thread's next alertable wait.
- **`XSetThreadProcessor`**: the game places each thread on a hardware thread (Worker0 and
  Database on 1, Worker1 and Stream on 2, Backend and Cinematic on 3, Worker2 on 4, Server and the
  Bink decoding threads on 5) and keeps per-processor buffers for the models it builds, indexed by
  the number at PCR+0x10C. The runtime only changed
  the KTHREAD's affinity, so all threads kept their creator's number, shared the buffers and the
  first level crashed while loading (`sub_823CAB70`). The thread now reads its new number.
- **The main thread on hardware thread 0**, as on the console
  (`GameDescriptor::main_thread_processor`; the other games keep 1): on 1 it shared Worker0's
  buffers and crashed the same way.
- **Message boxes**: `XamShowMessageBoxUIEx` read its result from the wrong argument (it has one
  more than `XamShowMessageBoxUI`); fixed for every game.
- **Pacing**: one vblank per frame: 60 FPS in the menus and the levels.
- The copy used here is a Spanish disc: its files carry only Spanish, whatever language the
  game is told (`XGetLanguage`, the launcher's profile).

## Status (2026-10-20)
- **Played by a person**: the user tried it and reports that it runs well and looks right (how
  far they got isn't recorded).
- **Boot and menus** (scripted pad): the opening videos (Bink), the brightness and safe area
  screens, the content warning, the main menu (Special Ops, Campaign, Multiplayer), the campaign
  menu and the difficulty choice. The settings (profile settings) and the save (`savegame.svg`)
  are written.
- **Campaign**: a New Game loads the opening (its loading video with subtitles, the in-engine
  intro with Price and Yuri, the MW3 title card) and goes on to Black Tuesday: the briefing video,
  the in-engine opening in New York, then gameplay. The scripted player walks up the street
  toward the objective marker, fires, takes damage; allies and enemies fight, with the HUD,
  compass, effects, subtitles and audio (XMA). 60 FPS throughout; tested for nine minutes.
- **Known issues**:
  - About one start in fifteen stops on a black screen after the first frame: the render thread
    holds Direct3D's lock and waits for the swap it queued to be retired by the vblank interrupt
    handler (device+16848 to reach device+16840, `sub_820A0EE8`), which never happens; the fast
    file loader then waits for that lock and the main thread for the loader. A restart gets past
    it. Not understood yet; a race between the swap queue and the interrupt handler (on the
    console Direct3D can mask interrupts on its processor, which does nothing here) is the main
    suspect.
  - In the intro level the snowy trees look flat white against a black sky (possibly a
    rendering difference; not compared with a console).
  - Choosing Multiplayer starts `default_mp.xex`, which isn't recompiled: the process ends.
  - Not tried: Special Ops, Survival, the DLC, split screen, later missions, saving mid-mission.

## Reproducing
From the Games on Demand package (`<package>` is the folder that holds `415608CB`):

```
build.bat
build\tools\x360tool\release\x360tool.exe god-extract "<package>\415608CB\00007000\<file>" extracted\mw3\game
build\tools\x360tool\release\x360tool.exe xex-extract extracted\mw3\game\default.xex extracted\mw3\default.bin --key keys\xex2_retail.key
python -I tools\scripts\find_helpers.py extracted\mw3\default.bin
cd games\mw3
..\..\build\thirdparty\XenonRecomp\XenonAnalyse\Release\XenonAnalyse.exe ..\..\extracted\mw3\game\default.xex switch_tables.toml
mkdir recomp3
..\..\build\thirdparty\XenonRecomp\XenonRecomp\Release\XenonRecomp.exe config.toml ..\..\thirdparty\XenonRecomp\XenonUtils\ppc_context.h
cd ..\..
build.bat release mw3
```

XenonRecomp writes into `out_directory_path` from `config.toml`, and that folder must exist and be
empty before it runs.

**Scripted route** to the first level (a new user folder; times from the first input poll, with a
warm shader cache):

```
ANYX360_INPUT=25:START,35:START,45:LRIGHT/0.5,48:A,52:LRIGHT/0.5,55:A,58:LRIGHT/0.5,61:A,66:START,72:A,78:A,84:A,92:DOWN,95:A,99:A,103:A
```

START twice skips the opening videos; the brightness and both safe area screens each need the
slider moved before A continues; A answers the content warning ("ask me later"), picks Campaign,
then DOWN and A start a New Game and A takes the default difficulty (Regular).
