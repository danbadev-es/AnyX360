// GPU: Xenia's Direct3D 12 Xenos emulation (thirdparty/xenia), driven by AnyX360's kernel.
// The title's statically linked Direct3D writes PM4 packets to the ring buffer and pokes the GPU
// registers at 0x7FC80000 (MMIO, handled by xe::Memory); Xenia's command processor renders them
// and presents into the game window.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace x360::gpu {

struct DisplayConfig {
  enum class Upscale { kBilinear, kCas, kFsr };

  uint32_t width = 1920;   // window client size
  uint32_t height = 1080;
  // Xenia's draw resolution scale: the title renders at 1280x720 x scale (integer only). 2x costs the
  // GPU about 4x as much as 1x.
  uint32_t render_scale = 1;
  Upscale upscale = Upscale::kFsr;  // scaling of the rendered image to the window (AMD FidelityFX)
  bool fxaa = false;                // FXAA on the final image
  bool vsync = true;                // present on the display's vertical blank (no tearing in fullscreen)
  bool fullscreen = false;
  // Target frame rate. Titles present every N vblanks (SetVblanksPerFrame), so the vblank interrupt
  // runs at target_fps x N.
  uint32_t target_fps = 60;
};

// How many vblanks the title waits between presents at its normal frame rate (Gears of War 3: 2,
// i.e. 30 FPS at 60 Hz). A property of the title, not a user setting. Call before Initialize.
void SetVblanksPerFrame(uint32_t vblanks);

// Creates the game window, the D3D12 graphics system and the vblank/interrupt thread. Must run on
// the UI (main) thread after the guest memory exists. Returns false if D3D12 is unavailable.
bool Initialize(const char* window_title, const DisplayConfig& config);
const DisplayConfig& GetDisplayConfig();  // UI thread
// Changes the target frame rate (the vblank rate) while running. UI thread.
void SetTargetFps(uint32_t fps);
void SetWindowTitle(const std::string& title);  // UI thread

// Loads (and keeps updating) the title's persistent shader and pipeline cache under cache_root,
// compiling cached pipelines in the background as Xenia does when it launches a title.
void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id);

// Runs the UI message loop until the window closes or the title exits; returns the exit code.
int RunMessageLoop();
// Asks the UI loop to exit (any thread).
void RequestQuit(int exit_code);

// Records a guest frame (VdSwap) for the performance overlay.
void NotifySwap(uint32_t frontbuffer_width, uint32_t frontbuffer_height);

// Vd* exports.
void InitializeRingBuffer(uint32_t physical, uint32_t size_log2);
void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2);
void SetInterruptCallback(uint32_t callback, uint32_t user_data);

}  // namespace x360::gpu
