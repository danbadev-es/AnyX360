# The `.anyx` extension: evaluation

**Status:** evaluated and designed; not implemented (decision of 2026-10-20).

## The idea
One file per prepared game, `<game>.anyx`, that the launcher writes from a copy it has already
prepared and reads back on another PC without converting anything: the same thing every time,
instead of copies in many formats and from many sources (disc folders, Games on Demand packages,
Xbox Live Arcade packages, ISOs, archives of each, with or without downloadable content and title
updates).

## What it would be
- **A 7z archive** (the launcher already ships 7-Zip and extracts 7z), store-only or LZMA2 with a
  fast level: game data is mostly compressed already (Bink videos, XMA audio, packed resources), so
  compressing gains little and costs time. The extension only marks what's inside.
- **`manifest.json` at the root**, written last:
  - the catalog id, the title, the title ID and the `default.xex` SHA-1 (which game, which version);
  - every file with its size and SHA-1;
  - the downloadable content packages included (already extracted, as the launcher installs them);
  - the AnyX360 version that wrote it, and the format version.
- **`game\`** (the prepared copy, as in `games\<id>\game`) and **`content\`** (the downloadable
  content). No title updates (AnyX360 plays the base executable), no saves or settings (those stay
  in `%LOCALAPPDATA%\AnyX360\<game>`), no keys, no covers.

## In the launcher
- **Export**: the ⋯ menu's "Save as .anyx…" writes it from a prepared game (progress, cancel, the
  space check the adds already do).
- **Import**: "Add game" recognises it by its manifest (not by its extension), checks the title ID
  and the hashes, and moves `game\` and `content\` into place: no Games on Demand, ISO or STFS
  conversion, so it is the fastest way in.
- Both are a few hundred lines on what exists: 7-Zip runs, `IdentifyCopy`/`CheckCopy`,
  `InstallDownloadableContent`, the jobs' progress.

## Benefits and costs
- One verified format: a copy that imports is complete and the same as the one exported.
- The downloadable content travels with the game.
- The size is about the copy's (4-8 GB for the current games), and exporting takes a few minutes.
- One more format to keep stable (the manifest's version).

## The legal limit
The terms of use require each player to own a legally obtained copy of every game they use with
AnyX360, and AnyX360 doesn't provide or distribute games. A format whose stated purpose is passing
games to other people would make AnyX360 a tool for distributing copies, which the terms forbid and
which puts the project at risk. So `.anyx` can only be offered as a **backup, or a way to move your
own prepared copy to another PC of yours**; giving a copy to someone else stays outside the terms
(each person adds their own copy). Nothing in the file would identify or restrict who uses it, and
it isn't encrypted: it is the player's own data.

Moving a prepared copy between your own PCs already works without `.anyx`: copy the
`games\<id>\game` folder (or your original copy) and add it as a folder ("Add game" → a folder).

## Decision
Not implemented in this round. If it is, as a backup/transfer between the player's own PCs, worded
that way in the launcher and the README, with the manifest above.
