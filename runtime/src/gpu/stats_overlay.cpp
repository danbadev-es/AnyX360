// Performance overlay drawn with Xenia's ImGui drawer over the presented frame (top-left corner,
// F1 toggles it): guest frame rate and frame time (from VdSwap), the process's video memory use
// (DXGI), and host and guest RAM.
#include "stats_overlay.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>

#include "third_party/imgui/imgui.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/memory.h"
#include "xenia/ui/d3d12/d3d12_provider.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>

namespace x360::gpu {

namespace {

using clock = std::chrono::steady_clock;

std::mutex g_swap_mutex;
std::deque<clock::time_point> g_swaps;  // the last second of VdSwap times

struct Sample {
  double fps = 0;
  double frame_ms = 0;
  double worst_ms = 0;
  uint64_t vram_used = 0, vram_budget = 0, shared_used = 0;
  uint64_t working_set = 0, commit = 0;
  uint64_t guest_virtual = 0, guest_physical = 0;
};

class StatsOverlay : public xe::ui::ImGuiDialog {
 public:
  StatsOverlay(xe::ui::ImGuiDrawer* drawer, xe::gpu::GraphicsSystem* graphics, xe::Memory* memory)
      : ImGuiDialog(drawer), graphics_(graphics), memory_(memory) {
    auto* provider = static_cast<xe::ui::d3d12::D3D12Provider*>(graphics->provider());
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (provider && SUCCEEDED(provider->GetDXGIFactory()->QueryInterface(IID_PPV_ARGS(&factory))))
      factory->EnumAdapterByLuid(provider->GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter_));
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) visible_ = !visible_;
    if (!visible_) return;
    auto now = clock::now();
    if (now - last_sample_ > std::chrono::milliseconds(500)) {
      Refresh();
      last_sample_ = now;
    }
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##anyx360_stats", nullptr, flags)) {
      const double mb = 1024.0 * 1024.0;
      ImGui::Text("%.0f FPS  %.2f ms (worst %.1f ms)", s_.fps, s_.frame_ms, s_.worst_ms);
      if (adapter_)
        ImGui::Text("VRAM %.0f MB / %.0f MB  (shared %.0f MB)", s_.vram_used / mb, s_.vram_budget / mb,
                    s_.shared_used / mb);
      ImGui::Text("RAM  %.0f MB (commit %.0f MB)", s_.working_set / mb, s_.commit / mb);
      ImGui::Text("Guest %.0f MB virtual + %.0f MB physical", s_.guest_virtual / mb, s_.guest_physical / mb);
    }
    ImGui::End();
  }

 private:
  void Refresh() {
    {
      std::lock_guard lock(g_swap_mutex);
      auto now = clock::now();
      while (!g_swaps.empty() && now - g_swaps.front() > std::chrono::seconds(1)) g_swaps.pop_front();
      s_.fps = double(g_swaps.size());
      s_.worst_ms = 0;
      for (size_t i = 1; i < g_swaps.size(); ++i)
        s_.worst_ms =
            std::max(s_.worst_ms, std::chrono::duration<double, std::milli>(g_swaps[i] - g_swaps[i - 1]).count());
      s_.frame_ms = g_swaps.size() > 1 ? std::chrono::duration<double, std::milli>(g_swaps.back() - g_swaps.front())
                                                 .count() /
                                             double(g_swaps.size() - 1)
                                       : 0;
    }
    if (adapter_) {
      DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
      adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
      adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
      s_.vram_used = local.CurrentUsage;
      s_.vram_budget = local.Budget;
      s_.shared_used = shared.CurrentUsage;
    }
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
      s_.working_set = counters.WorkingSetSize;
      s_.commit = counters.PrivateUsage;
    }
    const xe::BaseHeap* virtual_heaps[] = {memory_->LookupHeap(0x00010000), memory_->LookupHeap(0x40000000)};
    const xe::BaseHeap* physical_heaps[] = {memory_->GetPhysicalHeap()};
    uint32_t unreserved = 0, reserved = 0, used = 0, reserved_bytes = 0;
    memory_->GetHeapsPageStatsSummary(virtual_heaps, 2, unreserved, reserved, used, reserved_bytes);
    s_.guest_virtual = uint64_t(used) * 4096;
    unreserved = reserved = used = reserved_bytes = 0;
    memory_->GetHeapsPageStatsSummary(physical_heaps, 1, unreserved, reserved, used, reserved_bytes);
    s_.guest_physical = uint64_t(used) * 4096;
  }

  xe::gpu::GraphicsSystem* graphics_;
  xe::Memory* memory_;
  Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
  Sample s_;
  clock::time_point last_sample_{};
  bool visible_ = true;
};

std::unique_ptr<xe::ui::ImGuiDrawer> g_imgui;
std::unique_ptr<xe::ui::ImmediateDrawer> g_immediate;
StatsOverlay* g_overlay = nullptr;  // owned by the ImGui drawer's dialog list

}  // namespace

void StartStatsOverlay(xe::ui::Window* window, xe::gpu::GraphicsSystem* graphics, xe::Memory* memory) {
  g_imgui = std::make_unique<xe::ui::ImGuiDrawer>(window, 1);
  g_immediate = graphics->provider()->CreateImmediateDrawer();
  g_imgui->SetPresenterAndImmediateDrawer(graphics->presenter(), g_immediate.get());
  g_overlay = new StatsOverlay(g_imgui.get(), graphics, memory);
}

void NotifySwap() {
  std::lock_guard lock(g_swap_mutex);
  g_swaps.push_back(clock::now());
  if (g_swaps.size() > 1000) g_swaps.pop_front();
}

}  // namespace x360::gpu
