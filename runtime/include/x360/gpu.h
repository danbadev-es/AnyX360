// GPU: Xenia's Direct3D 12 Xenos emulation (thirdparty/xenia), driven by AnyX360's kernel.
// The title's statically linked Direct3D writes PM4 packets to the ring buffer and pokes the GPU
// registers at 0x7FC80000 (MMIO, handled by xe::Memory); Xenia's command processor renders them
// and presents into the game window.
#pragma once

#include <cstdint>
#include <filesystem>

namespace x360::gpu {

// Creates the game window, the D3D12 graphics system and the vblank/interrupt thread. Must run on
// the UI (main) thread after the guest memory exists. Returns false if D3D12 is unavailable.
bool Initialize(const char* window_title);

// Loads (and keeps updating) the title's persistent shader and pipeline cache under cache_root,
// compiling cached pipelines in the background as Xenia does when it launches a title.
void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id);

// Runs the UI message loop until the window closes or the title exits; returns the exit code.
int RunMessageLoop();
// Asks the UI loop to exit (any thread).
void RequestQuit(int exit_code);

// Vd* exports.
void InitializeRingBuffer(uint32_t physical, uint32_t size_log2);
void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2);
void SetInterruptCallback(uint32_t callback, uint32_t user_data);

}  // namespace x360::gpu
