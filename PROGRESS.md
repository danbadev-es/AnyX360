# PROGRESS: Dance Central 3 moved to the Games on Demand copy + title update + 20 DLC

Paused 2026-10-30 ~03:40 at the coordinator's request (the user went to sleep). Resume from here.

Branch: `worktree-agent-a8f46e3dc3ec615d4` (off main fc02e02), worktree
`C:\Users\DanielBarreto\Desktop\Code\danielbarretoes\AnyX360\.claude\worktrees\agent-a8f46e3dc3ec615d4`.
Sandbox: `C:\axdc3` (LOCALAPPDATA redirected there; kept as is, see below).
Do not touch games/dc2 or C:\axdc2 (another agent ports Dance Central 2). Never push. Never commit
docs/IDEAS.md. Remove this file and the probes before the final report.

## Resumed 2026-10-30 22:10: the song-key plan
- The old dump survives in main's `anyx\dc3.anyx` (READ ONLY, never modify): its `game/default.xex`
  (sha256 matches the manifest) extracted to `extracted\dc3\old\default.xex`, image `old.bin`.
- old.bin vs base.bin differ only in the 3 patched functions, 2 imports, and **the key table at
  0x82D13000 (4 x 16 bytes)**: the dump holds the real keys, retail the obscured ones. The TU's table
  is the same bytes at the same address. Index: mogg version <=13 -> 0, 14 -> 1, 15 -> 2, 16 -> 3
  (sub_82623130). **DC2's table (0x82CA2C38, 4 entries) is byte-identical to DC3's obscured table**,
  so the same 4 real keys cover DC2.
- `keys\title_keys.txt` (git-ignored) written by the new `tools/scripts/title_keys.py`
  (lines "<obscured hex> <real hex> # comment"). Runtime: `GameDescriptor::title_keys` (DC3 sets it),
  host loads keys\title_keys.txt (FindUpwards like xex2_retail.key) or stops with a message;
  XeKeysSetKey maps obscured -> real, a missing key ends the game with a message (exit 3).
  make_dist copies the file if present; keys/README.md documents it.
- Next: build, run a song (suspect 1 test in game), then the checks.

## 2026-10-31 02:40: checks done, wrapping up
- Pauses verified (Guide gesture and pad START with ANYX360_INPUT set for player 1 + INPUT_LIVE).
- DLC: the 0x271 ordinal was taken back out (with it the DLC songs are listed but refused and the
  list crashes in StreamReceiver); documented as open in docs/DANCECENTRAL3.md "Downloadable content".
- Bloom: Xenia's rtv path blows out the glass panels (same on the 0.28.0 base build); rov fixes it.
  DC3 sets render_target_path_d3d12 = "rov" (main.cpp xenia_options). Contact sheet in the session
  scratchpad dc3tu\shots\06_bloom_rtv_vs_rov.png.
- Remaining: rebase on main, build and run tests, remove PROGRESS.md, junctions, empty C:\axdc3.

## Results so far (2026-10-30 23:30, uncommitted work listed below)
- **Suspect 1 confirmed**: with keys\title_keys.txt, "Better Off Alone" Perform Easy played to the end:
  105,578 points, results screen (C:\axdc3\runs\song1). Save written (content\00000001\savegame).
  Screenshots kept in session scratchpad dc3tu\shots (01_*, 02_*).
- **DLC listing**: the game opened no package until `XexGetProcedureAddress(xam, 0x271)`
  (XamContentCreateInternal) was added to kProcedureExports (UNCOMMITTED, xboxkrnl_misc.cpp). Now it
  mounts all 20 (cnt00..13), builds songcache (194 KB) and the song list shows DLC + import songs.
- **DLC songs refused**: starting a DLC song (import pack "Poker Face" (83F3DA0F) and DC3 DLC "What Makes
  You Beautiful" (62422AFA)) shows "La descarga de contenido ... ya no está disponible o el perfil ...
  no ha iniciado sesión en Xbox LIVE" -> title screen. No DLC file is opened at all (ANYX360_LOG_FILES,
  ANYX360_TRACE=1). Message token likely `content_read_failure` (ContentMgr, sender 824FA1E8, callers
  824FA32C and 82C1DFA4). Probe games/dc3/src/probe.cpp (UNCOMMITTED, in CMakeLists) logs the guest
  backtrace at 824FA1E8: run, select WMYB (route: Perform list, hand_down x2 then 4x fast down + 4x up,
  lands near "What Makes You Beautiful"), START, grep PROBE.
- **Crash seen twice** after the DLC refusal / scrolling the list: null read in sub_82629A48+0x1E4
  (r3 = 0 from `lwz r3,28(r31)` vtable call), from 8260A280 / 825F87D0. Investigate after the refusal.
- Other uncommitted: FindTitleKey also accepts a real key as itself (+test), docs (Song keys,
  Reproducing, bring-up), README, CHANGELOG [Unreleased], section.txt, game.json size_gb back to 7.
- Helpers: C:\axdc3\tools\step.py <run> action:wait... (writes last.png), around.py, xrefs.py, strrefs.py;
  start.ps1 now also sets ANYX360_INPUT_LIVE=<run>\pad.txt (append "START" for the pad pause test).

## Status in one line (before resuming)
The update build boots, reads the update's patch ARK, loads the user's save and reaches the song
list, but **a song never leaves the loading screen ("CARGANDO")** in Perform or in Rehearse. That
is the blocker being investigated.

## Done
1. **Identified the copy** (all in `extracted\dc3`, git-ignored, keep it):
   - Archive `raw\DC3-DLC-TU-GOD.rar` extracted to `extracted\dc3\pkg` (complete: 7z "Everything is
     Ok", 73 files). GOD header `373307D9\00007000\A9080A6703719ECBD565` (+ .data, 40 files).
   - `extracted\dc3\game`: `god-extract` (2375 files, 6.18 GiB, complete).
   - Base `default.xex`: **0.0.0.5, media 2BE86B86**, retail key, entry 0x827FC450 (same version and
     media ID as the old re-packed dump).
   - TU `TU_0RJ61UP_000000K000000.0000000000085` (console cache form, content type 0x000B0000,
     "Dance Central 3 Title Update", 140 files, 2.19 MiB) stfs-extracted to `extracted\dc3\update`:
     `default.xexp` (**0.0.0.5 -> 0.0.1.5**), `gen\patch_xbox.hdr` + `patch_xbox_0.ark`,
     `grammar_update\<lang>\*.cfg`. Patched exe `extracted\dc3\update\default_0.0.1.5.xex`
     (x360tool xex-patch; also written by XenonRecomp). PE images: `extracted\dc3\base.bin`,
     `extracted\dc3\default_patched.bin` (+ .imports.txt / .sections.txt).
   - **Code vs the old target**: the GOD base recompiled with the old config (scratch output kept in
     the session scratchpad `dc3tu\base_recomp`) differs from main's `games\dc3\recomp6` only in 3
     functions and 2 imports: the old dump had been patched to use `XeCryptAesKey/XeCryptAesCbc`
     (AES state on the stack) where retail uses `XeKeysSetKey`(0x242)/`XeKeysAesCbc`(0x24B):
     wrappers `827FFA30`/`827FFAC8` (update `82800FE0`/`82801078`), caller `82623180`
     (update `82622C60`), key table `0x82D13000 + idx*16` (slot 0xE0, 16 bytes, feed NULL, decrypt).
     **Data (e.g. the key table) could not be compared**: the old xex is gone.
2. **Runtime** (commit f6ade08): `XeKeysSetKey`/`XeKeysAesCbc` key slots in
   `runtime/src/hle/xboxkrnl_misc.cpp`, byte-identical to the DC2 worktree's
   (`agent-accd5cb6334e56bc1`) so the merge is clean. Only DC3/DC2 import them (risk: none for others).
   Verified at run time: `XeKeysSetKey(E0, 82D13010, 16)` then `XeKeysAesCbc(E0, ..., 16, ..., 0, 0)`
   are called when a song loads and succeed.
3. **Retarget** (commit e158c74): `games/dc3/config.toml` (file_path GOD xex, patch_file_path,
   patched_file_path, out `recomp7`, helpers from find_helpers.py on the patched image, setjmp
   `0x82C64770` / longjmp `0x82C64340` now configured), `switch_tables.toml` regenerated (189
   tables, 2 unresolved; the base gives the same with the current analyser), all `src/` addresses
   remapped (table in `games/dc3/notes/tu_addresses.md`), `game.json` "update"
   {0.0.1.5, TU_0RJ61UP_000000K000000.0000000000085}, size_gb 9, archive_hint
   "Dance Central 3 (Games on Demand)", `main.cpp` sets update_package/version.
   `games\dc3\recomp7` generated (352 files, no ERROR lines) — keep it.
4. **Launcher** (built in this worktree, catalog = dc3 only): `--archive raw\DC3-DLC-TU-GOD.rar`
   with `--root C:\axdc3\release`, LOCALAPPDATA=C:\axdc3\Local: "ok: Dance Central 3 está preparado,
   con su contenido descargable (20). También se ha añadido su actualización del título (0.0.1.5)."
   (217 s). It installs **20 DLC packages**; the 10 avatar items (content type 0x9000) are skipped
   silently. The TU_ cache file is installed to `content\000B0000\TU_...` (default.xexp, gen, grammar_update).
5. **DLC content** (songs.dta entries per package): 11 single-song packs (Glad You Came, Gangnam
   Style, Let It Roll, Airplanes, Sorry for Party Rocking, Take Care, London Bridge, What Makes You
   Beautiful, Whip It, Lights, Call Me Maybe), 5 artist packs of 2 (Usher 01, Justin Bieber 01, Lady
   Gaga 02, Rihanna 02, Pitbull 01), 4 import packs: DC1 (32), DC1 DLC (32), DC2 (44), DC2 DLC (32)
   (rough count: lines starting "(" in songs.dta): about 161 songs, all 20 packages are songs.
   Not yet seen in game (the Quick sandbox has no DLC; C:\axdc3\Local has all 20).
6. **Docs, uncommitted parts now in the WIP commit**: `docs/DANCECENTRAL3.md` header table + new
   "The copy" and "Title update" sections (status/reproducing/DLC sections still to write);
   `games/dc3/notes` regenerated from the GOD exe (xex-info.txt, imports.txt, imported_functions.txt;
   new xex-info_0.0.1.5.txt, sections_0.0.1.5.txt).

## What I was doing: the song loading hang
- Runs: `C:\axdc3\runs\boot1..3` (frames, err.log with ANYX360_LOG_FILES in boot1; boot3 has
  thread dumps every 10 s from 400 s). Sandbox `C:\axdc3\Quick` (TU copied by hand, no DLC).
- Menus, title screen, main menu, DANCE, PERFORM, song list, difficulty, Start all work with the
  scripted player; the save loads (no first-run tutorials; `s00000001p0:\save.dat` opened).
- After START (Better Off Alone, Easy) the loading screen stays forever (60 FPS). Same in Rehearse
  "Sesión de canción completa" (Y.M.C.A.).
- Thread dump while stuck (boot3/err.log): main thread 11 in its loop (829F6C70 8250C728 8250FE4C
  823117A8 82311F44); thread 16 at 82CB3268 / KeWaitForMultipleObjects (82CA02AC 82CA0430 82C9EDA8);
  thread 17 in guest code lr 82C89E14 (82C700B8 82CB4098 82C9EEE0 82C9F884 82C9EDF4); others waiting.
- Suspects, in order:
  1. **Wrong song key** (mogg decryption): retail derives it with XeKeysAesCbc; the re-packed dump
     may also have changed the key table data at 0x82D13000 (data wasn't comparable), or the
     console's key slots may not use the raw key. Check offline: take a song's .mogg (DLC song, e.g.
     `content\00000002\15BAB2F1...\songs\gladyoucame\gladyoucame.mogg`), its version/header, derive
     the key as the game does (key table entry -> AES-ECB decrypt of the 16 bytes at 701A04D0) and
     see whether the decrypted Ogg starts with "OggS". Dump the 16-byte input/output in a probe.
  2. **setjmp/longjmp** newly configured (users: XGraphics ucode assembler 0x82BB..., D3DX effect
     code 0x82AD...): A/B by recompiling into a new folder (e.g. recomp8) without them.
  3. Something else in the TU code path: A/B with the GOD **base** exe (scratch `base_recomp`, old
     addresses = main's src) + the new XeKeys runtime: if songs load there, the TU retarget is the
     cause; if not, the keys are.
- Probes used (removed before this commit): KLOG_I at the top of XeKeysSetKey/XeKeysAesCbc.

## Next steps after the hang
3. Regression with the scripted player: menus, a Perform song to the end with scoring, Kinect Guide
   pause (`guide`), pad Start pause (ANYX360_INPUT); screenshots.
4. DLC: song list shows them; one DLC song to the end; note refused/import packs.
5. Saves: already loads in the Quick sandbox; confirm after a song (save written, reloaded).
6. Docs: DANCECENTRAL3.md status/DLC/reproducing/routes, tools/package/dc3/section.txt ("Your
   copy": the Games on Demand archive with its update and DLC), CHANGELOG [Unreleased], README if
   it names the disc copy.
7. Final: remove PROGRESS.md and probes, rebase on main, run build\tests\Release\test_*.exe and
   test_launcher_core (build them first: only test_launcher_core is built), remove junctions,
   empty C:\axdc3.

## Setting up again
- **Junctions** (removed at the pause): from the worktree, in PowerShell:
  `foreach ($n in "xenia","XenonRecomp","XenosRecomp") { $p = "thirdparty\$n"; [System.IO.Directory]::Delete($p, $false) ; New-Item -ItemType Junction -Path $p -Target "C:\Users\DanielBarreto\Desktop\Code\danielbarretoes\AnyX360\thirdparty\$n" }`
  (the folders are empty after `git checkout -- thirdparty`; delete them non-recursively first).
  While they exist, git diff/status need `--ignore-submodules=all`.
- **Build** (`build\` already configured with `-DANYX360_GAMES=dc3`; recomp7 compiled): use
  `C:\axdc3\tools\build.bat <targets>` (edit its `cd /d` if the worktree moved), started from
  PowerShell with `Start-Process -FilePath ...\build.bat -ArgumentList "dc3" -RedirectStandardOutput ... -PassThru`
  then `$p.PriorityClass = 'BelowNormal'`. Targets: dc3, anyx360_launcher, test_launcher_core,
  test_hle, test_avatar, test_dispatcher, test_title_update, test_guide, test_nui, test_nui_bridge, x360tool.
  The Bash tool refuses `cmd`; use PowerShell Start-Process.
- **Sandbox** `C:\axdc3` (kept):
  - `seed\AnyX360`: pristine copy of the user's `%LOCALAPPDATA%\AnyX360\DanceCentral3` + profiles +
    system + profile.txt + terms_accepted.txt (save.dat sha256 3a708f42...ecc4, same as the user's).
  - `Local\AnyX360`: launcher-installed (20 DLC + TU) + the seed; `terms_accepted.txt` holds THIS
    build's terms hash (ac0a0c25b0070ef8; dc3-only catalog; `C:\axdc3\tools` has no terms_hash.py —
    it is `scratchpad\terms_hash.py <build\launcher\launcher_terms.h> <out>`).
  - `Quick\AnyX360`: seed + TU copied by hand, no DLC (used by boot1-3).
  - `release`: launcher root (`bin\7z.exe`, `7z.dll`, `keys\xex2_retail.key`, `games\dc3\game` 6.2 GB
    installed by the launcher). Delete the key and everything at the end.
  - `tools`: helper scripts: `start.ps1 -Tag <t> -Local <LOCALAPPDATA> [-LogFiles] [-Pad ...]`
    (starts the build's DanceCentral3.exe with the scripted player reading `C:\axdc3\runs\<t>\script.txt`,
    frames every 5 s; PID in pid.txt), `step.py <run> action[:wait] ...` (append actions, wait for a
    frame, write `last.png`), `view.ps1`, `route_perform.sh` (fragile: the song list remembers its
    state; drive with step.py and look), `match.py`/`match_window.py` (shape matching base -> patched),
    `retarget.py` (the address map), `strrefs.py`, `callers.py`, `notes.py`, `base_config.toml`.
- **Menu route that worked** (save with tutorials done): wait ~50 s (frame 10), `menu_right`,
  `swipe_left/0.8` x4 (intro, MENÚ PRINCIPAL, BAILAR, ACTUAR) with ~6 s between, then in the song
  list `hand_down` x3 (Better Off Alone), swipe, `hand_up` x2 (FÁCIL), swipe, `hand_up` (EMPIEZA),
  swipe, `lower`, `dance/300`.
