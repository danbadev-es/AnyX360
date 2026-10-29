# Profiles

**Status:** implemented in 0.20.0 (profile format version 1). The first stage of the console
experience (profiles, then achievements, the Guide and the dashboard, then the avatar editor):
later stages add their files next to these.

## The idea
Like the console's, a profile belongs to a player, not to a game: one gamertag, its saves in every
game, its time played (later: its achievements and its avatar). The launcher asks "Who's playing?"
and signs that profile in as player 1; in a game, the sign-in screen the game asks for is a real
picker, so players 2-4 sign in as their own profiles with their own controllers and get their own
saves.

## Files
Everything is in AnyX360's folder of `%LOCALAPPDATA%` (`%LOCALAPPDATA%\AnyX360`, the parent of the
games' user folders). The code is `runtime/include/x360/profiles.h` (the profiles) and
`x360/profile.h` (who is signed in, the languages), in the `x360_runtime` library that the games
and the launcher both link.

```
profiles\<XUID>\profile.json          the profile
profiles\<XUID>\titles\<TitleID>.json  its time played in a title
profile.txt                            who is signed in on each slot, and the language
profile.txt.before-0.20.0              the file from before profiles, kept (the migration)
```

`<XUID>` is 16 upper-case hex digits; `<TitleID>` 8. Every file is written atomically: to a
temporary file next to it, flushed, then renamed over it, so a game and the launcher never read
half of one.

### profile.json
```json
{
  "version": 1,
  "xuid": "E0003A41C29F0B57",
  "gamertag": "Daniel",
  "gamerpic": "",
  "avatar": "",
  "language": "auto",
  "created": "2026-10-29T05:04:00Z"
}
```
- **xuid:** an offline XUID: `E000` and 48 random bits for a new profile (`RandomXuid`), never one
  of the four old ones below. The folder's name must match it; a copied or damaged file isn't a
  profile.
- **gamertag:** letters, digits and single spaces, at most 15 (`CleanName`); no two profiles share
  one in any letter case.
- **gamerpic:** a picture's file name in the profile's folder. Empty for now: the launcher and the
  picker draw the gamertag's initial on a colour from the XUID.
- **avatar:** the avatar description (the console's 1000-byte blob, `avatar_system.cpp`'s
  `WriteDescription`) in base64. Empty for now: games get the default avatars. The avatar editor
  fills it (stage 4).
- **language:** `auto` (Windows' display language) or a tag of `profile::Languages()` (`en`, `es`,
  `ja`...). The games are told player 1's profile's language; `auto` falls back to profile.txt's.
- **created:** ISO 8601, UTC. The profiles are listed in this order.

Unknown keys are ignored, so later stages can add theirs without a new version.

### titles\\<TitleID>.json
```json
{
  "version": 1,
  "title_id": "4D5308AB",
  "title": "Gears of War 3",
  "seconds_played": 7260,
  "last_played": "2026-10-29T06:10:41Z"
}
```
The runtime adds the time each signed-in profile has played every minute, when a player signs out,
when the game switches executables (MW3, BO2) and when it exits; the launcher shows it under the
selected game for the profile playing ("Daniel has played 2 h 1 min · last on 29 October 2026").
Players without a profile (below) aren't counted.

### profile.txt
```
# Who is signed in on each controller slot (profiles\<XUID>\profile.json) and the games' language.
slot1 = E000000012345678
slot2 = E0003A41C29F0B57
slot3 =
slot4 =
language = auto
```
`slot1` is the launcher's choice in "Who's playing?" (remembered: the screen is skipped while it
names a profile). The games' picker writes slots 2-4 when a player chooses a profile, so the next
game signs them in as the same profile when it signs them in at once ("automatic", "at_start").
One profile is on one slot at most: signing it in elsewhere moves it.

## Saves
Saves and each game's profile settings stay in the game's own folder, keyed by XUID, as before
(`profiles::UserFolder`, used by the runtime's `UserRoot`):
- the profile with XUID `E000000012345678` (player 1's before 0.20.0) uses the game's root folder
  (`<user_dir>\content`, `<user_dir>\profile`);
- any other uses `<user_dir>\players\<XUID>`.

That is the console's `Content\<XUID>\<TitleID>`, turned around. `.anyx` backups never had saves and
don't change. Deleting a profile renames its `profile.json` to `profile.deleted.json` and signs it
out of profile.txt: its saves, its time played and its folder stay.

## The migration (first start of 0.20.0)
Before 0.20.0 profile.txt held four names (`player1 = Daniel`...) and each slot played with a fixed
XUID, `E000000012345678` + slot. `profiles::LoadSignIn` makes profiles from that once, when the
`profiles` folder doesn't exist yet, and never moves or deletes a file:
1. profile.txt is copied to `profile.txt.before-0.20.0`.
2. Player 1 becomes a profile with XUID `E000000012345678`, named as before ("Player" without a
   name), so every game's root folder keeps its saves.
3. Each of players 2-4 with a name becomes a profile with its old XUID (`E000000012345679`...), so
   the games' `players\<XUID>` folders keep matching. Without a name, a player who has saves in a
   game (`<game>\players\<their XUID>`) gets a profile "Player 2"..., not signed in.
4. Each named player is signed in on their slot, and profile.txt is written in the new format.

Without a profile.txt (a player who never opened the launcher's old Players dialog, or a new one),
the launcher makes the "Player" profile with the first XUID (and those of step 3) and signs nobody
in, so "Who's playing?" shows it. A game started without the launcher (a development run, an empty
`LOCALAPPDATA`) makes nothing: players play as settings.toml's names with the old XUIDs, as before.

## Signing in, in a game
`runtime/src/hle/xam.cpp` (`InitUsers`, `SignInUser`, `SignInProfile`) and
`runtime/src/gpu/signin_picker.cpp`.
- **Player 1** is always signed in: as profile.txt's `slot1`, else the profile with the first XUID,
  else settings.toml's `[players] p1` name with the first XUID (development runs).
- **Players 2-4** sign in when the game shows its sign-in UI (`XamShowSigninUI`), as settings.toml's
  `players.sign_in` says (F2 > Players > Signing in):
  - `on_demand` (the default): **the picker.** `XamShowSigninUI` returns at once, as on the console;
    the game sees the system UI open (`XN_SYS_UIVISIBLE` 1, `XamIsUIActive`), pauses, and reads
    neutral controllers (`input::SetSystemUiOpen`) until it closes. The picker lists the profiles
    not signed in, then "Create profile" (the on-screen keyboard: the d-pad and A, B deletes, X a
    space, Y Shift, Start done; or the keyboard) and Cancel. A profile chosen signs in on the slot
    whose controller chose it (player 1's controller or the keyboard: the first free slot with a
    controller); the game gets `XN_SYS_SIGNINCHANGED` while the picker is still open, then
    `XN_SYS_UIVISIBLE` 0 when it closes, after as many players as the game asked for, or Cancel/B.
    Its text is English or Spanish, as the game's language. It grows with the window (its sizes times the window's
    height over 720 lines at its DPI: half again on a 1080p window), to be read from the couch.
  - `automatic`: no picker; up to the number asked of the free slots with a connected controller
    sign in at once, as their slot's profile (profile.txt), else as settings.toml's name with the
    slot's old XUID. This is what 0.19.0 and before did.
  - `at_start`: every slot with a device signs in when the game starts, as `automatic` does.
  - `manual`: only from the F2 panel.
- **Kinect titles' sign-in** (`XamShowNuiSigninUI`, made with gestures on the console) stays
  `automatic`: the picker can't be driven by the virtual Kinect.
- **Scripted runs** (`ANYX360_INPUT`, `ANYX360_INPUT_LIVE` or `ANYX360_NUI_SCRIPT` set) treat `on_demand` as `automatic`,
  so the routes in the docs keep passing; `ANYX360_SIGNIN_PICKER=1` shows the picker anyway, and the
  scripted presses drive it (`A@1` chooses for player 2).
- A player who signs in as another profile than before reads their own profile settings (the cache
  of `profile\*.bin` is per slot and is dropped on sign-in).

## The launcher
`tools/launcher/ui/app.cpp`:
- **"Who's playing?"** at start while profile.txt's `slot1` names no profile: a tile per profile and
  "Create profile". The header shows the gamertag playing (its gamerpic, then "Switch profile",
  which opens it again).
- **Profiles** (the gamertag in the header): every profile with the slot it is signed in on; a
  profile's menu has "Play as", "Rename…" and "Delete profile…" (asked first; nothing is erased),
  then "Create profile" and the games' language for the profile playing.
- A gamertag is typed with the keyboard or the on-screen keyboard (the controller's d-pad and A; B
  deletes, X a space, Y Shift, Start done).
- Screenshots: `--screenshot <png> --state who|profiles|newprofile` (made-up profiles, nothing
  written).

## Tests
- `test_hle`: profile.txt in both formats, the migration (names to profiles with their XUIDs, the
  backup, once only), `UserFolder`, time played, a first start, a nameless player 2 with saves.
- `test_launcher_core`: the migration of a user folder with saves (nothing moved), create, rename and
  delete (taken and empty gamertags, a deleted profile's saves kept), moving a profile between
  slots, damaged and copied profile files, atomic writes, the time played as shown.

## Checked (2026-10-29)
- **Migration with saves:** a sandbox user folder (LOCALAPPDATA redirected) with a profile.txt from
  before 0.20.0 (`player1 = Daniel`, `player2 = Ana`) and a Gears of War 3 campaign checkpoint made
  by 0.18.0 (New campaign, Normal) in the root folder. The new build made Daniel (`E000000012345678`)
  and Ana (`E000000012345679`), kept `profile.txt.before-0.20.0`, and the campaign menu offered
  Continue, which loaded the checkpoint (the save's SHA-256 unchanged). Gears skipped its
  first-start video settings, read from the same `profile\*.bin`.
- **Two profiles in split screen:** Modern Warfare 3 multiplayer (from the update, with
  `ANYX360_SIGNIN_PICKER=1` and live scripted input). Player 2 joined as a guest, pressed X, the
  picker opened over the paused game (no profile free: only "Create profile"), "Ana" was typed on
  the on-screen keyboard with player 2's pad and signed in on slot 2; the game showed Daniel and
  Ana, each with their own `cmr_history` save (the root folder and `players\E000…`), and both
  played a Team Deathmatch. Each profile got its time played; profile.txt remembered Ana on slot 2.
- The documented MW3 split screen route (`ANYX360_INPUT`, no picker) still reaches the lobby.
- A first start on a layout like a real one (no profile.txt, `players\E000000012345679` in two
  games) made "Player" and "Player 2" and signed nobody in.
- **Minecraft split screen** (0.19.0's `.anyx`, with its update and downloadable content): started
  from the launcher with a remembered profile (Steve, from an old profile.txt), it went straight in
  as Steve. With `ANYX360_SIGNIN_PICKER=1`, in a new world player 2 pressed START twice, the picker
  opened, "Alex" was made on the on-screen keyboard and signed in on slot 2, the screen split
  (Steve, Alex) and both moved and mined. Steve's world and settings are in the root folder,
  Alex's settings in `players\<his XUID>`; each got their time played.
- **Regressions** with the same build: Kinect Joy Ride (the scripted route to "How to drive"),
  Kinect Sports and Fruit Ninja Kinect (to their intro and title, player 1 signed in at startup,
  no picker) and Gears of War 3 Continue.
