// Performance overlay (FPS, frame time, VRAM, RAM) over the game window.
#pragma once

namespace xe {
class Memory;
namespace gpu {
class GraphicsSystem;
}
namespace ui {
class Window;
}
}  // namespace xe

namespace x360::gpu {

// Creates the overlay; call on the UI thread once the presenter is attached to the window.
void StartStatsOverlay(xe::ui::Window* window, xe::gpu::GraphicsSystem* graphics, xe::Memory* memory);

}  // namespace x360::gpu
