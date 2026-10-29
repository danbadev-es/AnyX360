# Profiles

**Status:** implemented in 0.20.0 (profile format version 1); the avatar and the gamerpic are
filled since 0.23.0 (the same version: the fields were there); guests since 0.24.0. The first stage of the console
experience (profiles, then achievements, the Guide and the dashboard, then the avatar editor):
later stages add their files next to these.

## The idea
Like the console's, a profile belongs to a player, not to a game: one gamertag, its saves in every
game, its time played, its achievements, its avatar and its gamerpic. The launcher asks "Who's playing?"
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
profiles\<XUID>\gamerpic.png          its gamerpic, if it has one (0.23.0)
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
- **gamerpic:** a picture's file name in the profile's folder: `gamerpic.png` (since 0.23.0, a
  picture of its avatar or of one of the player's games, chosen in the launcher), or empty: the
  launcher and the games' Guide and picker draw the gamertag's initial on a colour from the XUID.
- **avatar:** the avatar description (the console's 1000-byte `XAVATAR_METADATA`, as
  `x360tool::write_avatar_description` writes it) in base64 (1336 characters), filled by the
  launcher's avatar editor (0.23.0). Empty: the games give the player the default avatars. A game
  gives each player the avatar of the profile signed in on their slot ([AVATARS.md](AVATARS.md)).
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
- a guest ([Guests](#guests)) uses `<user_dir>\players\guest<n>`, emptied when a guest signs in
  with that number and when the game starts again;
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

## Guests
As on the console, a player without a profile can play as a **guest** of a signed-in one: "Daniel
(1)", "Daniel (2)"... (`kernel::SignInGuest`, `profiles::GuestXuid` and the helpers next to it in
`x360/profiles.h`).
- **Where:** the sign-in picker lists **Guest** ("Invitado" in Spanish) after the profiles, with the
  gamertag the guest will have ("as Daniel (1), nothing is kept"), unless the title's call says
  `XSSUI_FLAGS_DISALLOWGUEST` (0x01000000: Minecraft's "Connect to Xbox Live" asks for a profile
  only); and the Guide's Profiles: a free
  slot (players 2-4) offers "Choose a profile" (the picker) and **Guest** (on that slot). A guest's
  slot says "Guest" and signs out like any other. Player 1 is never a guest, so the launcher's
  "Who's playing?" has no guest.
- **The sponsor:** the first player signed in who isn't a guest: player 1, who always is (the
  console asks for a signed-in profile; AnyX360 always has one). Should there be none, the guest is
  called just "Guest". The guest number is the lowest of 1-3 no other guest has; the gamertag is the
  sponsor's, shortened so that "<gamertag> (n)" fits 15 characters ("ABCDEFGHIJK (2)").
- **The XUID** follows the console's guest form, which titles test with the XDK's `IsGuestXUID`:
  `0x0009` in the top 16 bits (the online form) with the guest number in bits 54-55 (`0x0049...`,
  `0x0089...`, `0x00C9...`), and the sponsor's low 48 bits (`E0003A41C29F0B57`'s guest 1 is
  `00493A41C29F0B57`). It is neither an offline XUID (`E...`) nor a plain online one.
- **What a title sees:** `XamUserGetSigninInfo` gives the flags `XUSER_INFO_FLAG_GUEST` (2), the
  guest number at +0x10 and the sponsor's user index at +0x14; `XamUserGetXUID`,
  `XamUserGetName` and the notifications (`XN_SYS_SIGNINCHANGED`) are those of any player.
- **Nothing is kept:** a guest is no profile (never in `profiles\`, never in profile.txt). Their
  achievements unlock nothing (no toast, nothing stored, not even for the session); their time
  played isn't counted; they get the default avatar and the gamerpic's initial.
- **Saves:** in `<user_dir>\players\guest<n>\` (`profiles::UserFolder` of a guest XUID), never the
  root folder or a profile's. The folder is emptied when a guest signs in with that number, and the
  three are removed when the game starts (`InitUsers`), so a guest's saves last while the game runs,
  as most titles keep them on the console. A title that switches executables (Modern Warfare 3's and
  Black Ops II's multiplayer) starts without its guests: they sign in again there.

## The launcher
`tools/launcher/ui/app.cpp`:
- **"Who's playing?"** at start while profile.txt's `slot1` names no profile: a tile per profile and
  "Create profile". The header shows the gamertag playing (its gamerpic, then "Switch profile",
  which opens it again).
- **Profiles** (the gamertag in the header): every profile with the slot it is signed in on; a
  profile's menu has "Play as", "Rename…", "Edit avatar…", "Change gamerpic…" and "Delete
  profile…" (asked first; nothing is erased), then "Create profile" and the games' language for the
  profile playing. The avatar editor and the gamerpic picker are in [AVATARS.md](AVATARS.md); the
  dashboard's Avatar page edits the profile playing's.
- **Gamerpics** show wherever a profile does (the header, the gamercard, the rows, "Who's
  playing?"), round; and in the games, in the Guide's header and the sign-in picker's rows (read
  again when the file changes). Without one, the initial.
- A gamertag is typed with the keyboard or the on-screen keyboard (the controller's d-pad and A; B
  deletes, X a space, Y Shift, Start done).
- Screenshots: `--screenshot <png> --state who|profiles|newprofile` (made-up profiles, nothing
  written).

## Tests
- `test_hle`: profile.txt in both formats, the migration (names to profiles with their XUIDs, the
  backup, once only), `UserFolder`, time played, a first start, a nameless player 2 with saves;
  guests: their XUIDs (the guest bits, the number, the sponsor's bits), gamertags, folders (and
  clearing only theirs), no profile, time played or slot of theirs, `XamUserGetSigninInfo`'s guest
  flag, number and sponsor, and an achievement unlocked for a guest that isn't kept.
- `test_guide`: a guest signed in and out with the Guide open, and without it.
- `test_launcher_core`: the migration of a user folder with saves (nothing moved), create, rename and
  delete (taken and empty gamertags, a deleted profile's saves kept), moving a profile between
  slots, damaged and copied profile files, atomic writes, the time played as shown.
- `test_avatar`: an avatar description through `profile.json` (base64) and back, byte for byte.
  `test_hle`: each slot's avatar from the profile signed in on it.

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

## Guests checked (2026-10-29)
In a sandbox (`LOCALAPPDATA` redirected, the games added from their `.anyx` backups with their
updates), with one profile, Daniel, on slot 1 and `ANYX360_SIGNIN_PICKER=1` with scripted pads:
- **Minecraft**, in a new world: the Guide (`GUIDE`), Profiles, Player 2, **Guest**: "Daniel (1)"
  signed in on slot 2 with its toast, and Profiles showed "Daniel (1) · Guest · Sign out". Player 3's
  "Choose a profile" opened the picker with "Guest — as Daniel (2), nothing is kept" (cancelled).
  Then player 2 pressed START twice: the screen split, the lower view named "Daniel (1)".
  Minecraft's own join (`XamShowSigninUI(1, 0x01010001)`) asks with `XSSUI_FLAGS_DISALLOWGUEST`, so
  its picker offers the profiles and Create profile only, as the console's would.
- **Modern Warfare 3 multiplayer**, Split Screen: player 2 joined ("Invitado 2", the game's own
  guest), pressed X (sign-in profile), the picker (`XamShowSigninUI(1, 0x10000)`) offered Guest,
  and A signed "Daniel (1)" in; the game showed "DANIEL (1)" in player 2's quadrant, wrote the
  guest's `mpdata` and `cmr_history` to `players\guest1\`, and both started a Team Deathmatch on
  Seatown in split screen. The next start of the game removed `players\guest1`.
