# PROGRESS (agent E, wave 1: items 23, 24, 27) - remove before the report
- [x] tools/scripts/release.ps1 (+ make_anyx.ps1, terms_hash.py, lib.ps1); -WhatIf and real dry run
      (clone in build\dryrun, removed) passed after 3 fixes; docs/RELEASING.md
- [x] tools/scripts/stop_stray.ps1 (tested)
- [x] tools/regress/ (routes.json, run_regress.ps1, compare.ps1, set_baseline.ps1), docs/REGRESSION.md
- [ ] baseline: run1 into S\regress\run1 (all 20, dist 0.32.0) -> review shots, fix routes, rerun some,
      set_baseline.ps1, baseline/README.md with the table
- [ ] end: stop_stray on worktree + C:\axreg, delete C:\axreg, remove PROGRESS.md, rebase onto main
Notes: DCC needs the v2 avatar pack (user's LOCALAPPDATA has Reach's v1 -> crash at 10 s): taken from ks.anyx.
Machine busy during run1: other agents' MW3MP copies + 12 clang-cl compiles.
