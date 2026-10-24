# The `.anyx` format

**Status:** implemented in 0.14.0 (format version 1). Evaluated on 2026-10-20 and implemented on
2026-10-23. Format version 2 (0.16.0) adds the title update a game is played with.

## The idea
A `.anyx` file is one file per prepared game. The launcher writes it from a copy it has already
prepared and reads it back on this PC or another of the player's own, without converting anything.
It is the same thing every time, instead of copies in many formats and from many sources: disc
folders, Games on Demand packages, Xbox Live Arcade packages, ISOs and archives of each, with or
without downloadable content and title updates. Adding a game from it is the quickest way in:
nothing is converted, and every file is checked.

## The legal limit
- **What the terms require.** The terms of use require each player to own a legally obtained copy
  of every game they use with AnyX360, and AnyX360 doesn't provide or distribute games.
- **What follows for `.anyx`.** A format whose purpose is passing games to other people would make
  AnyX360 a tool for distributing copies, which the terms forbid and which puts the project at risk.
  So `.anyx` is offered only as a **backup**, or as a way to move your own prepared copy to another
  PC of yours.
- **How it is worded.** The launcher's message after saving, the README and the release README say
  so: giving a copy to someone else is outside the terms, because each person adds their own copy.
- **What the file is.** Nothing in the file identifies or restricts who uses it, and it isn't
  encrypted or signed: it is the player's own data.
- **Terms unchanged.** The terms text didn't change for it: §2 (your own copy) and §6 (no
  distributing game files) already cover it.

## Layout
A 7z archive with the files stored, not compressed (`-mx0`), one stream per file (`-ms=off`):

```
manifest.json           written by the launcher, described below
game\...                the prepared copy, as in games\<id>\game (the folder holding default.xex)
content\<package>\...   each downloadable content package, extracted as the launcher installs it
                        (%LOCALAPPDATA%\AnyX360\<game>\content\00000002\<package>)
update\<package>\...    format version 2: the title update the game is played with, extracted as
                        the launcher installs it (...\content\000B0000\<package>)
```

What is never in it:
- **Saves.** They live in `content\00000001`, next to the packages, and are never taken.
- **Title updates of games played without one**, and other versions of a game's update.
- **The rest:** settings, keys, covers and icons.

What is left out of the copy:
- **`$SystemUpdate`** at its root: the console's system update that discs carry (11 MB on Kinect
  Sports and Kinect Joy Ride).
- **Folder junk:** `Thumbs.db`, `desktop.ini` and `.DS_Store` anywhere.
- **Links:** links inside the copy aren't followed.

What is kept:
- **The Avatar data** (`AvatarAssetPack` in `game\`), which avatar games need.
- **Empty folders.**

**Why the files are stored, not compressed.** Game data is mostly compressed already (Bink videos,
XMA audio, packed resources). Measured on Kinect Adventures (3.20 GB):

| | Size | Reading it back (`7z t`) |
|---|---|---|
| Stored (`-mx0`) | 3.20 GB | 4.2 s |
| LZMA2 level 1, multithreaded | 2.93 GB (−8.5%) | 7.5 s |

The rule was to compress if it saved at least 10%; it doesn't, so a backup is about the copy's own
size.

## manifest.json
UTF-8 JSON (it can be read with `7z e -so <file> manifest.json`):

```json
{
  "format": "anyx",
  "format_version": 1,
  "written_by": "AnyX360 0.14.0+1a2b3c4",
  "game": {"id": "ks", "title": "Kinect Sports", "title_id": "4D5308C9",
           "version": "0.0.0.2", "media_id": "655F2429"},
  "content": ["1B844357CD9D6F41D2A8A2DD7E17E133222E71B64D", "DE2CF375…"],
  "folders": ["game/some/empty/folder"],
  "files": [{"path": "game/default.xex", "size": 9437184, "sha256": "…"},
            {"path": "content/1B84…4D/…", "size": 1234, "sha256": "…"}]
}
```

- **`game`**: the catalog's ID and title, and `default.xex`'s execution info: the title ID, the
  version and the media ID.
- **`content`**: the downloadable content packages, by their folder names.
- **`update`** (format version 2, only when the backup has one): the title update's `package` (its
  folder name), the `base_version` it updates (`default.xex`'s) and the `version` it makes:
  `"update": {"package": "TU_16L627D_000000C000000.0000000000083", "base_version": "0.0.0.3",
  "version": "0.0.1.3"}`. Its files are under `update/<package>/`.
- **`folders`**: the empty folders.
- **`files`**: every file, with its size and its SHA-256 (lowercase hex), sorted by path, with `/`
  between folders.
- **Not stored:** timestamps, source paths, and anything about the player or the PC.

## Writing (the ⋯ menu's "Save as .anyx…", or `AnyX360.exe --export <id> --to <file>`)
1. **The copy.** It is the extracted one, or the player's folder for a copy used in place. It must be
   this game, of the version AnyX360 plays, and complete (`IdentifyCopy`, `CheckCopy`).
2. **The output.** It can't be inside the launcher's `games` folder (removing or preparing the game
   again would delete it), nor inside the copy.
3. **What goes in.** The copy, plus each package in `…\content\00000002` that has files and that the
   game takes (`NeedsTitleUpdate`: Black Ops II's and Modern Warfare 3's are never taken), plus, for
   a game played with its title update, the update installed in `…\content\000B0000\<package>`.
   - **The format version written:** 2 when the backup has an update, else 1, so earlier releases
     still read backups of games without one.
4. **The checks.** Room on the output's drive, and refusal on a FAT32 drive when the backup is over
   4 GB.
5. **Hashing.** Every file is hashed: 0-30 % of the progress.
6. **Staging.** 7-Zip writes the archive in one pass from two places: a staging folder
   (`games\.work\export`) holds `manifest.json`, an NTFS junction `game` → the copy,
   `content\<package>` junctions → each package, and an `update\<package>` junction → the update.
   - **The list.** A list (`files.lst`) names the manifest, then every file and empty folder by its
     relative path. Each item is a file path, so 7-Zip opens the files through the junctions and
     stores plain entries.
   - **The command:**
     ```
     7z a -t7z -mx0 -ms=off -sse -bsp1 -bso0 -bse1 -sccUTF-8 -scsUTF-8 <file>.partial @files.lst
     ```
     It runs from the staging folder: 30-99 % of the progress. `-sse` makes a file that can't be
     read fail the export instead of being left out.
   - **When junctions can't be made.** On a drive without junctions (not NTFS) the staging goes to
     the temporary folder. A copy on a network folder can't be the target of a junction: the
     player copies it to the PC first.
7. **Finishing.** The `.partial` file's listing is checked against the manifest, then the file is
   renamed to its name.
8. **Cleaning up.** On failure or cancel the `.partial` is deleted. The junctions are always removed,
   and only the links themselves: removal in the launcher never goes through a link.

## Reading ("Add game", drag and drop, `--archive <file>`)
1. **Recognising it.** An archive is recognised by its listing, whatever its name: `manifest.json`
   (under 16 MB) and `game\default.xex` at the root. The manifest is extracted alone first.
   - Without `"format": "anyx"`, it is an ordinary archive and goes on as one.
   - A `format_version` above 2 is refused: "saved by a newer AnyX360". Releases before 0.16.0
     refuse version 2 files the same way.
2. **The checks, before anything big is extracted:**
   - **The game:** the title ID must be one of this release's games, of the version it plays.
   - **The paths:** every path must be relative, under `game/`, a listed package's folder or the
     update's (`update/<package>/`), with no `..` and no drive letter.
   - **The listing:** the archive must hold exactly the manifest's files, of their sizes.
3. **Asking and space.** Replacing a game already in the library is asked first. Room is checked on
   the release's drive and on `%LOCALAPPDATA%`'s drive.
4. **Extracting.** Everything is extracted into `games\.work` (to 70 %).
5. **Checking every file.** Each file's SHA-256 is compared with the manifest's, to 99 %. A
   difference names the file: "damaged or changed".
6. **The copy.** The real `default.xex` must still be this game, and the copy complete. It then
   moves to `games\<id>\game`, replacing an earlier copy, extracted or used in place. Then its icon,
   and the Avatar data.
7. **The packages.** Each package replaces its folder in
   `%LOCALAPPDATA%\AnyX360\<game>\content\00000002`, unless this release leaves it out (a pack
   that needs the title update): those are counted and noted.
8. **The title update** (version 2) replaces `%LOCALAPPDATA%\AnyX360\<game>\content\000B0000\<package>`
   (the package name the game's `game.json` gives) if it is the version the game is played with.
   Another version is noted and left out, and so is an update of a game played without one.
9. **Without its update.** A backup of a game played with its title update that doesn't have it (a
   version 1 file, saved before the game was played with it) brings back the copy and its content,
   then says the update is missing and how to add it: the game isn't ready until it is added.
10. **The saves** in `00000001` are never touched.

## Versions and compatibility
- **The pinned version.** Every game's `game.json` names the version and media ID of the executable
  AnyX360 was recompiled from: `"xex": {"version": "0.0.0.2", "media_id": "655F2429"}`.
- **Why.** A copy of another version has other code, even under the same title ID: an update, or a
  re-release. For example, the Kinect Sports Ultimate Collection's first disc is Kinect Sports
  version 6, while AnyX360 plays version 2.
- **Title updates.** A game recompiled from its title update keeps `"xex"` as the disc's version
  (what the copy holds) and names the update: `"update": {"version": "0.0.1.3", "package":
  "<package>"}`. The update's version is checked when it is installed, from its `default.xexp`'s
  delta patch descriptor, and in a backup's manifest.
- **Where it is checked.** The launcher refuses another version wherever it identifies a copy: a
  folder, an archive's probe (before the big extraction), a disc image, an `.anyx`'s manifest and its
  extracted `default.xex`. Other executables in the copy (multiplayer's `default_mp.xex`) must be the
  same version too.
- **The media ID is recorded, not enforced:** other pressings of the same version share the code.
- **The manifest's format version.**
  - Readers ignore keys they don't know, so a later writer can add some.
  - A change older readers would get wrong raises `format_version`, and they refuse the file. Version
    2 added `update/` files, which a version 1 reader would take for damage.

## Limits
- **Size:** a backup is about the size of the copy and its content (0.1-8 GB for the current
  games), and needs that much room to write and to add back.
- **FAT32 drives** can't hold the bigger backups (over 4 GB): use exFAT or NTFS.
- **Network folders:** a copy used in place from a network folder can't be saved from there; copy
  it to the PC first.
- **Integrity, not authenticity:** the hashes detect damage, not deliberate changes (nothing is
  signed).
- **Saves aren't included:** copy `%LOCALAPPDATA%\AnyX360\<game>` yourself to move them.
- **Avatar data:** Doritos Crash Course's avatars come from the Avatar data on Kinect Sports' disc,
  which isn't in Doritos' backup: add Kinect Sports too.
