# Forza Motorsport 4: where the port stands (branch fm4)

Remove this file when the port is finished. Status and technical notes: docs/FORZAMOTORSPORT4.md.

## Reached
Logos, title ("Press A to start"), profile save, brightness, Top Gear intro video, then the first
race's loading screen (Bernese Alps), where it stays forever (9 minutes tried). Nothing is read
after the Alps audio zones; `Media\tracks\Alps\bin.zip` is never opened; no export is stuck.

## Next
1. Find what the loading state waits for: main thread loop `822DADD8` -> `822A7308` ->
   `8243F330` -> `8287AE48` (pow-heavy, runs every iteration); game thread `823F6D60` waits each
   frame on the event at `[82DE1AA0]` (set by `826D1938`). Ideas not tried: a GPU fence (the
   command processor "waits for memory" ~45% on the loading screen), XMA state, the
   `XamContent*`/`XamCache*` stubs, the 14 unresolved table-driven `bctr` of the analyser.
2. Then: career/World Tour, Free Play, garage/upgrades/tuning, paint editor, storefront, replays,
   Rivals offline, the disc-2 and DLC cars (scope from the user, in that order).

## Setting up again
- XenonRecomp: branch `anyx360-fm4` (ef84908) is in the main checkout's submodule repository;
  the fm4 commit pins it. Build it, regenerate `games/fm4/recomp`, `recomp_xmedia`,
  `recomp_speech` (docs: Reproducing).
- Test runs: `--user <sandbox>` with the content packages extracted to
  `<user>\content\00000002\<package>` plus `<user>\content\Headers\00000002\<package>.header`
  (display name, 128 big-endian UTF-16 characters), `[players] p1 = { device = "pad" }` in
  `<user>\settings.toml`, `ANYX360_CAPTURE_DIR`/`ANYX360_CAPTURE_MS` for frames,
  `ANYX360_INPUT=28:A,40:A,60:A~15x6`, a fresh `<user>\content\00000001` for the first-run path.
- `tests/launcher` (test_launcher_core) had 2 failures here, in the `.anyx` tamper checks
  (lines 1057, 1070), unrelated to these changes and not investigated.
