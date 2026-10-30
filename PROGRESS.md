# PROGRESS: Dance Central 3 moved to the Games on Demand copy + title update + 20 DLC

Branch: worktree-agent-a8f46e3dc3ec615d4 (off main fc02e02). Sandbox: C:\axdc3 (LOCALAPPDATA redirected).
Do not touch games/dc2 or C:\axdc2 (another agent). Never push. Never commit docs/IDEAS.md.

## Setup done
- thirdparty\xenia, XenonRecomp, XenosRecomp are junctions to the main checkout (remove as links only
  at the end: [System.IO.Directory]::Delete(path,$false), then `git checkout -- thirdparty`).
- extracted\dc3\pkg: the archive raw\DC3-DLC-TU-GOD.rar extracted (GOD, 20 DLC in 00000002, 10 avatar
  items in 00009000, TU_0RJ61UP_000000K000000.0000000000085 at the root).
- extracted\dc3\update: the TU (stfs-extract): default.xexp, gen\patch_xbox.hdr + patch_xbox_0.ark,
  grammar_update\<lang>\*.cfg (140 files).
- extracted\dc3\game: god-extract of the GOD package.

## DLC (content type 2): 20
14 song packs (single songs and artist packs) + 4 import packs (DC1, DC1 DLC, DC2, DC2 DLC).
00009000: 10 avatar items (ignored by the launcher: not type 2).

## Steps
1. [ ] identify base xex version/media ID, TU version
2. [ ] retarget: config.toml patch_file_path, new recomp folder, re-find hooks
3. [ ] regression: menus, Perform song to end, Guide pause, pad Start pause
4. [ ] DLC songs
5. [ ] saves (copy of user's DanceCentral3)
6. [ ] docs
