// The display's vertical blank, tracked on a thread with IDXGIOutput::WaitForVBlank for the monitor
// showing the game window: a phase-locked estimate of when the screen refreshes and how often.
#pragma once

#include <cstdint>

namespace x360::gpu::host_vblank {

void Start(void* hwnd);

// A recent display vblank (QPC time) and the refresh period (QPC ticks). False until the estimate
// is stable, or when the monitor can't be waited on.
bool Estimate(int64_t& time, double& period);

}  // namespace x360::gpu::host_vblank
