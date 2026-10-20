// The Kinect debug window ("AnyX360 Kinect"): everything the virtual Kinect sees and does, in a
// window of its own outside the game's, for a second monitor. The camera's image with every body's
// skeleton, the depth image, the play space from above and from the side, the main player's joints,
// the sensor's state and the last 10 seconds of frame timings.
//
// It runs on its own thread with its own Direct2D window (a second presenter on the game's swap
// chain would add presents to its queue), at below-normal priority, 30 times a second. It has no
// owner window and shows without activating, so opening it doesn't take the game's focus or release
// its captured cursor. A game-independent library: the nui:: functions it calls are the game's.
#pragma once

#include <functional>
#include <string>

namespace x360::kinect_window {

// Opens the window where it was (`placement`, from Placement; "" the first time: on a monitor
// without `game_window`, else on the game's). `changed` is called on the window's thread when the
// window is closed (open false) or moved (open true), with its placement.
void Open(const std::string& placement, void* game_window,
          std::function<void(bool open, const std::string& placement)> changed);
// Any thread.
void Close();
bool IsOpen();

}  // namespace x360::kinect_window
