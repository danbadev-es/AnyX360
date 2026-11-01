# Releasing

A release is one command, `tools\scripts\release.ps1`, run in the main checkout on `main`. It does
what took about ten manual steps before 0.33.0, logs each one and stops at the first failure.

## The command

```
powershell -ExecutionPolicy Bypass -File tools\scripts\release.ps1 -Version 0.33.0 ^
  -Summary "What this release brings, in one sentence" ^
  [-Branch <branch to merge>] [-FromWorktree <worktree with new games\<id>\recomp*>] ^
  [-AnyxGames halo3,reach -PasswordNote <note with the archives' password>] [-Push] [-WhatIf]
```

| Parameter | What it does |
|---|---|
| `-Version` | The new version, `X.Y.Z`, newer than `VERSION`. |
| `-Summary` | The line under the version's heading in CHANGELOG.md, and the commit message's subject (`Release X: <summary>`). |
| `-Branch` | Merged first with `git merge --ff-only`. |
| `-FromWorktree` | A worktree whose `games\<id>\recomp*` folders are copied in (they aren't in git). Folders already here are left alone: a regeneration goes into a new folder name. |
| `-Date` | The release date (default today). |
| `-DistDir` | Where the release folder goes (default `dist`). |
| `-AnyxGames`, `-PasswordNote`, `-AnyxOut` | Makes those games' `.anyx` backups with the new release (`tools\scripts\make_anyx.ps1`, default output `anyx\`). The password of the encrypted archives is read from the note at run time; it is never printed, logged or put on a command line. |
| `-Push` | Pushes `main` and the tag. Agents never pass it. |
| `-WhatIf` | Runs the checks (preconditions, game list, terms hash) and prints what every other step would do. Changes nothing. |
| `-SkipBuild`, `-SkipTests` | For dry runs only. |
| `-Jobs` | Parallel build jobs (default 6). |
| `-WorkDir` | Logs, the IDEAS.md copy, the terms hash (default `%TEMP%\anyx360-release`). |
| `-Trailer` | The commit message's last line (default the Co-Authored-By line; empty for none). |

## The steps

0. **Preconditions.** On `main`, `vX` not tagged yet, `X` newer than `VERSION`, no uncommitted
   changes except `docs\IDEAS.md`, the release folder not there yet. A copy of `docs\IDEAS.md` and
   its SHA-256 go to the work folder.
1. **Merge** `-Branch` with `--ff-only`.
2. **Recompiled sources** copied from `-FromWorktree`.
3. **Release edits.** `VERSION`; the README badge `version-X-2ea44f`; in CHANGELOG.md
   `## [Unreleased]` stays (empty) and its entries move under `## [X] - <date>` with the summary, and
   the compare links become `[Unreleased]: .../compare/vX...HEAD` and `[X]: .../compare/vOLD...vX`.
   An empty `[Unreleased]` stops the release.
4. **Commit** `Release X: <summary>` (only VERSION, README.md and CHANGELOG.md) and the annotated
   tag `git tag -a vX -m "AnyX360 X"`.
5. **IDEAS.md out.** `docs\IDEAS.md` is the user's own list, never committed: `git checkout
   docs/IDEAS.md`, so the release isn't marked dirty (`X+commit.dirty`).
6. **Game list, then build.** Every `games\*\game.json` must have each of its targets' recompiled
   sources (`out_directory_path` in its config holding `ppc_config.h`), and every `<TARGET>_PPC_DIR`
   in `build\CMakeCache.txt` must be the folder its config names. A stale `GOW3_PPC_DIR` once made
   CMake skip Gears of War 3 silently for several releases. Then `build.bat release` at BelowNormal
   priority (`CMAKE_BUILD_PARALLEL_LEVEL` = `-Jobs`), and the same check again.
7. **Tests.** Every `build\tests\Release\test_*.exe` and `build\Release\test_launcher_core.exe`.
8. **Package.** `tools\package\make_dist.ps1 -NoBuild -OutDir <DistDir>` (its own build call fails:
   vswhere isn't found from it).
9. **Catalog check.** The "Packaged ..." line must name `<DistDir>\AnyX360-X` exactly (a `+commit`
   version means HEAD isn't the tag or the tree is dirty) and every game's title, and every game's
   executables must be in `bin\`.
10. **The player's installs.** `games\*` and `logs` of every older `AnyX360-*` folder in the dist
    folder move into the new one; then the old folder and its zip are deleted. Refused while a
    process runs from an old folder, or if links are left in it.
11. **IDEAS.md back** from its copy, its SHA-256 checked. This also happens when any step after 5
    fails.
12. **Terms hash.** FNV-1a 64 of the launcher's two terms texts (`build\launcher\launcher_terms.h`),
    written to the work folder: sandboxes need it in `terms_accepted.txt`
    (`tools\scripts\terms_hash.py` computes the same from Bash).
13. **Push** with `-Push` only.
14. **.anyx** with `-AnyxGames`: make_anyx's report must end with "password found in the report
    files: no", and every game must PASS.

## Before and after
- **Before:** refresh the shipped shader caches (`python -I tools\package\merge_shader_caches.py
  shader_cache "%LOCALAPPDATA%\AnyX360" <scratch>`, from Bash) so first plays don't compile shaders.
- **After:** run the regression battery against the previous baseline (docs/REGRESSION.md).
- **Submodules:** push the XenonRecomp and xenia branches the release pins before `-Push`.

## When a step fails
The log says which step failed and why. Steps 1-4 are ordinary git changes (`git reset --hard
ORIG_HEAD`, `git tag -d vX` undo them). After step 8, delete the half-made `dist\AnyX360-X` and its
zip before running again. If step 10 refused because a game or the launcher runs from the old
folder, close it and move `games\*` and `logs` by hand, then delete the old folder.

## Stray processes
`tools\scripts\stop_stray.ps1 -Path <folder>` lists the processes whose executable or command line
mentions a folder (and their children); `-Stop` stops them. Agents run it on their worktree and
sandboxes before reporting, so no hung shell keeps a folder locked.
