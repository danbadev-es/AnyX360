AnyX360 · version {VERSION}
==========================================================
Developed by danbadev-es.

Xbox 360 games recompiled to run natively on Windows. This version plays {GAMES}.
This folder does NOT include any game: you need your own legal copy of each game you play (an
archive with the disc - .7z, .rar or .zip - or a folder with the disc's files).

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
   The launcher is in English and Spanish: switch the language in the window's footer.
3. Click "Add game…" (or the "+" tile) and choose where your copy is:
   - "From an archive (.7z, .rar, .zip)": it is extracted into the "games" folder (a few
     minutes); you can delete the archive afterwards;
   - "From a game folder": the folder is used where it is (nothing is copied).
   The launcher recognises which game it is. Add each game you have the same way.
4. Choose a game in the gallery and click "Play" (or double-click it).

Right-click a game to change its cover, open its logs, prepare it again or remove it from the
library. The first time each screen appears it may stutter a little while its graphics are
prepared.


PROGRAM KEYS (every game)
  F1                  Statistics (FPS, memory; the webcam's frame rate and latency in Kinect games)
  F2                  Settings: resolution, fullscreen, upscaling, 30/60 FPS, VSync, mouse
                      sensitivity, each player's device and, in Kinect games, the Kinect tab
  F11 or Alt+Enter    Fullscreen
  Alt+Tab             Releases the mouse


SAVED GAMES AND SETTINGS
Each game keeps them in %LOCALAPPDATA%\AnyX360\<game> (paste %LOCALAPPDATA%\AnyX360 into File
Explorer's address bar). They are kept when you move to a new version.


IF SOMETHING GOES WRONG
- If a game closes with an error, the launcher tells you. Select the game, click "Logs" and send
  the last_run.log file (and previous_run.log if there is one; in Kinect games also
  pose_bridge.log, in the "logs" folder), with the version ({VERSION}) and what you were doing.
- If it freezes, close it and send those logs anyway.
- There is no online play (the Xbox 360's Xbox Live no longer exists).

{SECTIONS}

Includes 7-Zip (www.7-zip.org) to extract archives{COMPONENTS_NOTE} and other open-source
components; their licenses are in the "licenses" folder.

AnyX360 is not affiliated with {OWNERS}. {TRADEMARKS} are trademarks of their respective owners.

Developed by danbadev-es.
