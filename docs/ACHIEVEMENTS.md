# Achievements

**Status:** implemented for 0.21.0. The second stage of the console experience (profiles, then
achievements, the Guide and the dashboard, then the avatar editor), on the profiles of
[PROFILES.md](PROFILES.md).

## The idea
As on the console, a game unlocks its achievements for the profile that plays it, each once and
for good: a toast says so at the bottom of the screen, the game's own achievements screen lists it,
and the launcher shows each game's list and the profile's gamerscore. Nothing goes online; the
achievements earned on a real console aren't imported.

## Where they come from: the title's SPA
Every title carries its dashboard data (its SPA) as an XDBF database, a resource of its XEX named
after the title ID; the launcher already read its icon there. Its sections (be16 section, be64 ID):

| Section | ID | What |
|---|---|---|
| 1 (metadata) | `XACH` | the achievements: be16 count, then 0x24 bytes each: be16 ID, be16 name, unlocked and locked description string IDs, be32 image ID, be16 gamerscore, 2 bytes, be32 flags, 16 bytes |
| 1 | `XSTC` | the default language (be32) |
| 2 (images) | image ID | PNG pictures (0x8000: the title's icon) |
| 3 (strings) | language | an `XSTR` table per language: be16 count, then {be16 ID, be16 length, UTF-8} |

The string tables are numbered as the console's languages up to 9 (1 English, 2 Japanese, 3 German,
4 French, 5 Spanish, 6 Italian, 7 Korean, 8 traditional Chinese, 9 Portuguese), then 10 simplified
Chinese, 11 Polish, 12 Russian. An achievement's flags hold its type (bits 0-2) and whether its
locked description is shown (0x8; without it the achievement is secret until unlocked). The parser
is `tools/x360tool/src/xdbf.cpp` (`xdbf_achievements`, `xdbf_string`, `xdbf_title_list`), in the
`x360_formats` library the runtime and the launcher both link; the structures follow
`thirdparty/XenonRecomp/XenonUtils/xdbf.h`.

A title update may bring another SPA (more achievements, fixed strings), so the list a game uses is
the one in the image it runs: the base patched with its update.

## Files
In AnyX360's folder of `%LOCALAPPDATA%`, next to the profiles. The code is
`runtime/include/x360/achievements.h` (`x360_runtime`). Every file is written atomically, as the
profiles' are.

```
titles\<TitleID>\achievements.json            the title's list, in every language its SPA has
titles\<TitleID>\<ImageID>.png                each achievement's picture
profiles\<XUID>\achievements\<TitleID>.json   what the profile has unlocked, and when
```

### titles\\<TitleID>\\achievements.json
Written by the game when it starts if the list there came from another SPA (`source`, a hash of
it), so a title update's list replaces the base's; and by the launcher from the copy's
`default.xex` when it shows a game that has never run.

```json
{
  "version": 1,
  "title_id": "584111F7",
  "title": "Minecraft",
  "source": "9C1E5A0F3B7D2E41",
  "achievements": [
    {
      "id": 1,
      "gamerscore": 10,
      "image": "00000001",
      "flags": 9,
      "strings": {
        "en": { "name": "Taking Inventory", "locked": "Open your inventory.", "unlocked": "..." },
        "es": { "name": "...", "locked": "...", "unlocked": "..." }
      }
    }
  ]
}
```

### profiles\\<XUID>\\achievements\\<TitleID>.json
```json
{
  "version": 1,
  "title_id": "584111F7",
  "title": "Minecraft: Xbox 360 Edition",
  "unlocks": [
    { "id": 1, "gamerscore": 10, "date": "2026-10-29T08:12:31Z" }
  ]
}
```
Each achievement once: a second unlock keeps the first date. The gamerscore is kept with it, so the
profile's total (every title's file added up) needs no title list. A damaged file reads as nothing
unlocked. Unknown keys are ignored.

## In a game
`runtime/src/hle/xam.cpp` (section "achievements"):
- **XUserWriteAchievements** (XGI message `0x000B0008`: {be32 count, XUSER_ACHIEVEMENT* {be32 player,
  be32 ID}}) unlocks each for the player's profile. A player without a profile (a development run
  without the launcher) gets the toast once a session and nothing is kept.
- **XamUserCreateAchievementEnumerator** returns the real list, as XACHIEVEMENT_DETAILS (0x24 bytes:
  ID, label, description and unachieved description pointers, image ID, gamerscore, FILETIME
  unlocked, flags with 0x20000 once unlocked), with the strings the title asks for (flags 1, 2, 4;
  UTF-16 after the array, 464 bytes an item), in the game's language (else English). Its owner is
  the XUID given, else the player; anyone else has nothing unlocked here.
- **XamReadTileToTexture** gives an achievement's picture (tile type 0, the image ID) or the title's
  icon (type 1) as ARGB pixels.
- **XamShowAchievementsUI** (a game's "Achievements" menu: Minecraft's main menu)
  shows the system's list over the game: the player's achievements, the latest unlocked first, with
  their pictures, gamerscore and dates. The game pauses behind it, as behind the sign-in picker
  (`XN_SYS_UIVISIBLE`, `XamIsUIActive`, neutral controllers); the d-pad scrolls, B closes it.
  Kinect titles' version (`XamShowNuiAchievementsUI`) still returns at once.
- **The toast** (`runtime/src/gpu/achievement_toast.cpp`): bottom centre, "Achievement unlocked",
  "<gamerscore>G - <name>" and the picture, about six seconds, with a short chime; several in a row
  show one after another; the gamertag shows with more than one player signed in. English or
  Spanish, as the game's language; it grows with the window like the picker. F2 > Display > Overlay
  turns it and its sound off (`achievement_toasts`, `achievement_sound` in settings.toml).

## In the launcher
- **Achievements** next to Play (and in the ⋯ menu) opens the selected game's list for the profile
  playing: unlocked first (the latest first) with their date, then the locked ones (dimmed, with a
  padlock; secret ones say so), the pictures, and "3 of 93 unlocked · 30 of 2195 G" with a bar.
  The button shows the gamerscore earned of the game's.
- **The gamerscore** of the profile playing is in the header, after the gamertag, and each profile's
  in Profiles.
- Screenshots: `--screenshot <png> --state achievements` (a made-up list and profiles, nothing read
  or written) and `--state gameachievements` (the selected game's page from the files there; no
  executable is read in screenshots).

## Tests
- `test_launcher_core`: a made-up SPA (three achievements, English and Spanish string tables, two
  pictures): XACH and XSTR parsing, the fallback to English, secret flags, damaged data; the cache
  written and read back; the launcher reading a game's list from the cache; unlocks once each with
  their first date, a damaged file, the totals over two titles; the strings shown.
- `test_hle`: the same kind of SPA loaded as a title's: the enumerator with and without strings
  (layout, pointers after the array, paging, another title's), XUserWriteAchievements through
  XMsgStartIORequest (once, the unlock flag and time) and an achievement's picture through
  XamReadTileToTexture.

## Checked (2026-10-29)
Minecraft: Xbox 360 Edition (0.19.0's `.anyx`, with its title update), in a sandbox
(`LOCALAPPDATA` redirected), a profile signed in on slot 1:
- **The list from the update**: at start the game wrote 93 achievements (2195 G) and their 93
  pictures to `titles\584111F7\`. The base `default.xex`'s SPA has 20: the launcher, with no list
  there, read those 20 from the copy; the game's next start replaces them (another `source`).
- **An unlock**: in a new world (the route in [MINECRAFT.md](MINECRAFT.md)), Y opened the inventory
  and the game unlocked "Taking Inventory" (10 G): the toast showed at the bottom with its picture,
  and the profile's `achievements\584111F7.json` got it with its date. The game reads the list
  first through the enumerator (93 items, flags 0x20: no strings).
- **The game's own list**: the main menu's Achievements opened the system's list (1 of 93
  unlocked, 10 of 2195 G, the unlocked one first), the d-pad scrolled it and B closed it; the game
  resumed (`XN_SYS_UIVISIBLE` 1, then 0).
- **The launcher**: "Achievements 10/2195 G" next to Play, the page with the same list, and "10 G"
  after the gamertag (`--state gameachievements` over the sandbox).

Not checked in a game yet: Gears of War 3's and Fruit Ninja Kinect's achievements (they import the
same functions: Gears also XamShowAchievementsUI, Fruit Ninja Kinect the enumerator, its pictures and
XamShowNuiAchievementsUI), the Spanish toast, and several players.
