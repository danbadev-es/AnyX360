// Display changes while running (settings panel, hotkeys). UI thread unless noted.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "x360/gpu.h"

namespace xe::ui {
class Window;
}

namespace x360::gpu {

// Runs `fn` on the UI thread outside of any paint (any thread).
void PostToUIThread(std::function<void()> fn);

xe::ui::Window* GameWindow();
// The window's client area in physical pixels (DPI-independent). In fullscreen the size is kept and
// applied when leaving it.
void ApplyWindowSize(uint32_t width, uint32_t height);
void SetFullscreenMode(bool fullscreen);
void SetUpscale(DisplayConfig::Upscale upscale);
void SetFxaa(bool fxaa);
void SetVsync(bool vsync);
void SetDisplayLock(bool lock);
// The vblank's relation to the display, for the panel: the display's refresh rate (0 if unknown)
// and whether the vblank is locked to it.
void DisplayLockState(double& display_hz, bool& locked);

// Saves the settings and relaunches the executable without the display flags (so the saved ones
// apply). Exits on success; returns the error otherwise.
std::string Restart();

// The settings panel (F2).
void StartSettingsPanel();
void ToggleSettingsPanel();
bool IsSettingsPanelOpen();
void CloseSettingsPanel();

}  // namespace x360::gpu
