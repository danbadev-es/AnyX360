AnyX360 - Dance Central 3 · version {VERSION}
==========================================================
Developed by danbadev-es.

Xbox 360's Dance Central 3, recompiled to run natively on Windows, with a webcam in place of
the Kinect.
This folder does NOT include the game: you need your own legal copy of Dance Central 3 (the .7z
archive with the disc, or a folder with the disc's files).

By using this software you accept the terms of use in TERMS.txt, which the launcher shows you the
first time you open it. The changes in each version are listed in CHANGELOG.md.


REQUIREMENTS
- 64-bit Windows 10 or 11.
- A DirectX 12 graphics card with up-to-date drivers.
- A processor with 4 or more cores: the pose estimation that replaces the Kinect runs on it.
- A webcam (USB or built in), 720p at 30 FPS or better.
- Room to dance: 2-3 m (7-10 ft) clear in front of the webcam, and good light.
- About 7 GB free on the drive where this folder is, if you prepare the game from a .7z archive
  (it is extracted into it).


FIRST TIME
1. Extract this .zip into a regular folder, for example Documents or D:\Games
   (not inside "Program Files").
2. Open "AnyX360 Dance Central 3.exe".
   If Windows says "Windows protected your PC", click "More info" and then "Run anyway" (the
   program is not digitally signed).
   The first time you will see the terms of use: read and accept them to continue.
   The launcher is in English and Spanish: switch the language in the window's footer.
3. Click "Prepare" and choose where your copy is:
   - "From a .7z archive": it is extracted into the "game" folder (a few minutes);
   - "From a game folder": the folder is used where it is (nothing is copied).
4. Click "Play".

The first time each screen appears it may stutter a little while its graphics are prepared.


THE WEBCAM
Windows must let desktop programs use the camera: Settings > Privacy & security > Camera, turn
on "Camera access" and "Let desktop apps access your camera".

- Put the webcam under or over your TV or monitor, between knee and chest height, pointing at
  where you will dance.
- Stand 2-3 m away, so the camera sees all of you, from head to feet.
- Light the room from the front or the side. A bright window behind you makes it hard to see you.
- Wear clothes that stand out from the wall behind you.

The game sees you through "pose_bridge" (in bin\pose_bridge), a helper program that turns the
webcam's image into the Kinect's skeleton. The game starts it and closes it by itself.

If the game doesn't see you, press F2 and open the "Kinect" tab: it shows what the camera sees
and lets you adjust it:
  Camera                           which webcam, if you have more than one
  My camera already mirrors        turn it on if raising your right hand moves the skeleton's left
  Field of view                    your webcam's (most are 60-78 degrees)
  Smoothing                        more: steadier but later; less: quicker but jittery
  Players                          1 or 2
  Pose model                       Fast uses less of the processor; Accurate tracks better
  Recentre on me                   stand where you will dance and press it
  Restart camera                   if the camera stopped (for example another program took it)


MENUS (as with a Kinect)
- Raise your right arm out to the side: the cursor follows your hand. Move the hand up and down
  to choose.
- Swipe your arm across your body to select.
- Items marked "LEFT HAND" (back, options): raise your left arm instead and swipe it across your
  body.
- Pause during a song: hold your right arm up and out at 45 degrees, as the game's tutorial shows.
  With an Xbox controller connected, its Start button also pauses.

Without a webcam you can still use the menus with the mouse: F2 > Kinect > Source "Mouse". The
right hand follows the cursor, left click selects, right click goes back.


TWO PLAYERS
In Perform and Battle, the second player steps into view next to the first. Keep about a metre
between you: a single webcam has trouble when people overlap.


PROGRAM KEYS
  F1                  Statistics (FPS, memory, the webcam's frame rate and latency)
  F2                  Settings: resolution, fullscreen, upscaling, VSync and the Kinect tab
  F11 or Alt+Enter    Fullscreen


PRIVACY
The webcam's images are processed only on your computer, in real time; they are never saved or
sent anywhere. The game shows them on screen, for example in the photos it takes while you
dance, as it did with the Kinect.


SAVED GAMES AND SETTINGS
They are in %LOCALAPPDATA%\AnyX360\DanceCentral3 (paste it into File Explorer's address bar).
They are kept when you move to a new version.


IF SOMETHING GOES WRONG
- If the game closes with an error, the launcher tells you. Click "Logs" and send the
  last_run.log and pose_bridge.log files (and the "previous" ones if there are), with the
  version ({VERSION}) and what you were doing.
- If it freezes, close it and send those logs anyway.


KNOWN ISSUES
- The pause gesture hasn't been confirmed with a webcam yet. If it doesn't pause, use a
  controller's Start button, or let the song end.
- Voice commands don't work ("Say Xbox"): there is no microphone support.
- A webcam is not a Kinect: it has no depth camera, so the depth is estimated. Turning your back
  to the camera, very fast moves, and two players overlapping can be misread.
- There is a little more delay than with a Kinect (shown in F1 as "latency").
- There is no online play (the Xbox 360's Xbox Live no longer exists).


Includes 7-Zip (www.7-zip.org) to extract the archive, and MediaPipe, OpenCV, NumPy and Python in
the pose bridge, among other open-source components; their licenses are in the "licenses" folder
(the pose bridge's in licenses\pose_bridge).

AnyX360 is not affiliated with Microsoft, Xbox or Harmonix. Xbox, Xbox 360, Kinect, Dance Central
and Harmonix are trademarks of their respective owners.

Developed by danbadev-es.
