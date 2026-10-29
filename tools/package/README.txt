AnyX360 · version {VERSION}
==========================================================
Developed by danbadev-es.

Xbox 360 games recompiled to run natively on Windows. This version plays {GAMES}.
This folder does NOT include any game: you need your own legal copy of each game you play (an
archive of any kind, a disc image, a Games on Demand or Xbox Live Arcade package, or a folder with
the disc's files).

By using this software you accept the terms of use in TERMS.txt, which the launcher shows you the
first time you open it. The changes in each version are listed in CHANGELOG.md.


REQUIREMENTS
- 64-bit Windows 10 or 11.
- A DirectX 12 graphics card with up-to-date drivers.
- Free space on the drive where this folder is, for each game you add from an archive (it is
  extracted into it): {SIZES}.
{WEBCAM_REQUIREMENTS}

FIRST TIME
1. Extract this .zip into a regular folder, for example Documents or D:\Games
   (not inside "Program Files").
2. Open "AnyX360.exe".
   If Windows says "Windows protected your PC", click "More info" and then "Run anyway" (the
   program is not digitally signed).
   The first time you will see the terms of use: read and accept them to continue.
   The launcher is in English and Spanish: switch the language at the top right.
3. Choose "Add game" (the "+" tile), or drop your copy on the window, as you have it:
   - "From a file": an archive of any kind (.7z, .rar, .zip, in parts or not, .tar.gz...), a disc
     image (.iso), or a Games on Demand or Xbox Live Arcade package. It is extracted into the
     "games" folder (a few minutes); you can delete the original afterwards. A password-protected
     archive asks for its password.
   - "From a folder": the folder with the disc's files is used where it is (nothing is copied).
   - An AnyX360 backup (.anyx, see BACKUPS below): the game goes back as it was saved.
   The launcher recognises which game it is, and that it is the version AnyX360 plays. The game's
   downloadable content in the copy is installed too. Add each game you have the same way.
4. Choose a game and press "Play" (or click its cover again).

The launcher works with the mouse, the keyboard (arrows, Enter, Esc) or an Xbox controller (A
select, B back, Y more, X compatible games, Start play). "..." (or a right click, or Y) changes a
game's cover, opens its logs, prepares it again, saves it as an .anyx backup or removes it from the
library. "Compatible
games", at the bottom, shows every game this version plays and the ones coming. The first time
each screen of a game appears it may stutter a little while its graphics are prepared.

PROFILES: as on the console, each player has a profile with their gamertag and their own saves in
every game. The launcher asks "Who's playing?" (Switch profile, at the top right, asks again); the
gamertag next to it opens Profiles: create, rename, delete (the saves stay) and the language the
games use, Windows' by default (a game whose copy doesn't have it uses its own). In a game, when
another player joins, the game's sign-in screen lists the profiles: each player chooses theirs, or
creates one, with their own controller. The names you set before this version became profiles,
with their saves.

YOUR AVATAR: the dashboard's Avatar page (or "Edit avatar..." in a profile's menu, in Profiles)
dresses the profile's Xbox Avatar: body, skin, height, hair, face, clothes and their colours,
glasses, hats... Up and down choose what to change, left and right change it, the right stick turns
the avatar, Y makes a random one, Start saves it. Doritos Crash Course, Kinect Sports and Kinect
Joy Ride then give each player the avatar of their profile. "Gamerpic..." chooses the profile's
picture: its avatar, or a picture from one of your games. The avatar parts come with the avatar
games (Kinect Sports, Kinect Joy Ride): add one of them first.

THE GUIDE: in a game, the Xbox button opens the console's menu and pauses the game: Resume,
Achievements, Profiles (sign players in or out), Settings and "Xbox Home" (back to the launcher; the
progress you haven't saved is lost). Holding Back and Start for a second, or Home on the keyboard,
opens it too. If the Xbox button opens Windows' Game Bar instead: Windows 11, Settings > Gaming >
Game Bar > turn off "Allow your controller to open Game Bar" (Windows 10: Settings > Gaming > Xbox
Game Bar > "Open Xbox Game Bar using this button on a controller").

CONSOLE MODE: "Console mode", at the top right, makes the launcher a dashboard for the controller,
in its window without borders or full screen (Settings page): Home (the game you played last, your
gamercard), Games, Achievements, Profiles, Avatar, Settings and Add a game, changed with LB and RB.
A game opens in the same place and the dashboard comes back when you leave it, as one window.
"Desktop mode", in its Settings page, brings back the usual window. In both modes the launcher
stays open while you play and comes back when the game ends.


PROGRAM KEYS (every game)
  F1                  Statistics (FPS, memory; the webcam's frame rate and latency in Kinect games)
  F2                  Settings: resolution, fullscreen, upscaling, 30/60 FPS, VSync, mouse
                      sensitivity, each player's device and, in Kinect games, the Kinect page
  F3                  In Kinect games: the Kinect window, with everything the virtual Kinect
                      sees (put it on a second monitor; it remembers where it was)
  F11 or Alt+Enter    Fullscreen
  Home                The Guide (as the Xbox button)
  Alt+Tab             Releases the mouse


SAVED GAMES AND SETTINGS
Each game keeps them in %LOCALAPPDATA%\AnyX360\<game> (paste %LOCALAPPDATA%\AnyX360 into File
Explorer's address bar). They are kept when you move to a new version.


BACKUPS (.anyx)
"..." > "Save as .anyx..." writes a game you have added, with its downloadable content, into one
file: a backup of your copy, which "Add game" takes back on this PC or another of yours, checked
file by file and without converting anything (the quickest way to add a game). Your saved games
aren't in it: copy %LOCALAPPDATA%\AnyX360\<game> as well if you move to another PC. An .anyx file is
your own copy of the game: don't give it to anyone, each player adds their own copy (see TERMS.txt).


IF SOMETHING GOES WRONG
- If a game closes with an error, the launcher tells you: press "Open logs" there (or "..." >
  "Open logs") and send
  the last_run.log file (and previous_run.log if there is one; in Kinect games also
  pose_bridge.log, in the "logs" folder), with the version ({VERSION}) and what you were doing.
- If it freezes, close it and send those logs anyway.
- There is no online play (the Xbox 360's Xbox Live no longer exists).

{SECTIONS}

Includes 7-Zip (www.7-zip.org) to extract archives{COMPONENTS_NOTE} and other open-source
components; their licenses are in the "licenses" folder.

AnyX360 is not affiliated with {OWNERS}. {TRADEMARKS} are trademarks of their respective owners.

Developed by danbadev-es.
