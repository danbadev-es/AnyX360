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

// Creates the ImGui drawer and the (hidden) overlay; call on the UI thread once the presenter is
// attached to the window.
void StartStatsOverlay(xe::ui::Window* window, xe::gpu::GraphicsSystem* graphics, xe::Memory* memory);

// Shows or hides the overlay (UI thread). While no ImGui dialog is shown, the drawer is detached
// from the presenter and the GPU thread presents frames directly.
void SetStatsOverlayVisible(bool visible);
bool IsStatsOverlayVisible();

}  // namespace x360::gpu
