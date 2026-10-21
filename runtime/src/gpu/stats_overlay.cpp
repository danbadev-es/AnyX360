// Performance overlay drawn with Xenia's ImGui drawer over the presented frame (top-left corner,
// F1 toggles it; ui_theme's look): guest frame rate and frame time (from VdSwap), frame interval graphs, the
// process's video memory use (DXGI), host and guest RAM, and the virtual Kinect (Kinect games).
#include "stats_overlay.h"
#include "frame_timing.h"
#include "gpu_internal.h"
#include "ui_theme.h"
#include "x360/gpu.h"
#include "x360/nui.h"
#include "x360/settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>

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
std::atomic<uint32_t> g_frontbuffer_width{0}, g_frontbuffer_height{0};

struct Sample {
  double fps = 0;
  double frame_ms = 0;
  double worst_ms = 0;
  uint64_t vram_used = 0, vram_budget = 0, shared_used = 0;
  uint64_t working_set = 0, commit = 0;
  uint64_t guest_virtual = 0, guest_physical = 0;
  uint32_t presents_gpu = 0, presents_ui = 0;
  std::string kinect;  // empty for games without a Kinect
  std::string battery;  // empty when plugged in
};

class StatsOverlay : public xe::ui::ImGuiDialog {
 public:
  StatsOverlay(xe::ui::ImGuiDrawer* drawer, xe::ui::Window* window, xe::gpu::GraphicsSystem* graphics,
               xe::Memory* memory)
      : ImGuiDialog(drawer), window_(window), graphics_(graphics), memory_(memory) {
    auto* provider = static_cast<xe::ui::d3d12::D3D12Provider*>(graphics->provider());
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (provider && SUCCEEDED(provider->GetDXGIFactory()->QueryInterface(IID_PPV_ARGS(&factory))))
      factory->EnumAdapterByLuid(provider->GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter_));
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    auto now = clock::now();
    if (now - last_sample_ > std::chrono::milliseconds(500)) {
      Refresh();
      last_sample_ = now;
    }
    using namespace theme;
    ImGui::SetNextWindowPos(ImVec2(Px(12), Px(12)), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color::Vec(color::Rgb(0x121417, 215)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(12), Px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Px(8), Px(3)));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##anyx360_stats", nullptr, flags)) {
      const double mb = 1024.0 * 1024.0;
      ImGui::PushFont(Title());
      ImGui::TextColored(color::Vec(s_.fps >= 55 ? color::kGreenBright : s_.fps >= 28 ? color::kAmber : color::kRed),
                         "%.0f FPS", s_.fps);
      ImGui::PopFont();
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%.2f ms, worst %.1f ms", s_.frame_ms, s_.worst_ms);
      ImGui::PushFont(Small());
      const DisplayConfig& display = GetDisplayConfig();
      uint32_t scale = display.render_scale;
      const char* upscale = display.upscale == DisplayConfig::Upscale::kFsr   ? "FSR"
                            : display.upscale == DisplayConfig::Upscale::kCas ? "CAS"
                                                                              : "bilinear";
      auto line = [](const char* label, const char* format, auto... values) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(Px(64));
        ImGui::Text(format, values...);
      };
      line("Render", "%ux%u (%ux), output %ux%u %s%s%s", g_frontbuffer_width.load() * scale,
           g_frontbuffer_height.load() * scale, scale, window_->GetActualPhysicalWidth(),
           window_->GetActualPhysicalHeight(), upscale, display.fxaa ? " +FXAA" : "",
           window_->IsFullscreen() ? ", fullscreen" : "");
      if (adapter_)
        line("VRAM", "%.0f / %.0f MB (shared %.0f MB)", s_.vram_used / mb, s_.vram_budget / mb, s_.shared_used / mb);
      line("RAM", "%.0f MB (commit %.0f MB)", s_.working_set / mb, s_.commit / mb);
      line("Guest", "%.0f MB virtual + %.0f MB physical", s_.guest_virtual / mb, s_.guest_physical / mb);
      if (!s_.kinect.empty()) line("Kinect", "%s", s_.kinect.c_str());
      if (!s_.battery.empty()) {
        ImGui::TextDisabled("Power");
        ImGui::SameLine(Px(64));
        ImGui::TextColored(color::Vec(color::kAmber), "%s: plug in for steady frames", s_.battery.c_str());
      }
      // Frame intervals: the title's submissions (VdSwap) and what reached the screen (presents of new
      // frames), 0-50 ms; a steady 60 FPS is a flat line at 16.7 ms.
      line("Presents", "%s thread", s_.presents_ui > s_.presents_gpu ? "UI" : "GPU");
      float values[120];
      ImGui::Dummy(ImVec2(0, Px(2)));
      size_t count = timing::RecentIntervals(timing::Event::kVdSwap, values, 120);
      ImGui::PlotLines("##swap", values, int(count), 0, "frames", 0.0f, 50.0f, ImVec2(Px(300), Px(34)));
      count = timing::RecentIntervals(timing::Event::kPresentGpuThread, values, 120);
      ImGui::PlotLines("##present", values, int(count), 0, "presents", 0.0f, 50.0f, ImVec2(Px(300), Px(34)));
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
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
    timing::Summary t = timing::Summarize(1.0);
    s_.presents_gpu = t.presents_gpu_thread;
    s_.kinect.clear();
    if (settings::Get().kinect.enabled) {
      const nui::Status k = nui::GetStatus();
      char text[160];
      const char* people = k.bodies == 1 ? "person" : "people";
      if (k.source == "webcam" && k.camera_running)
        std::snprintf(text, sizeof(text), "webcam %.0f fps, %u %s, pose %.0f ms, latency %.0f ms", k.camera_fps,
                      k.bodies, people, k.inference_ms, k.latency_ms);
      else if (k.source == "webcam" || k.source.empty())
        std::snprintf(text, sizeof(text), "%s", k.message.empty() ? "not started" : k.message.c_str());
      else
        std::snprintf(text, sizeof(text), "%s, %u %s", k.source.c_str(), k.bodies, people);
      s_.kinect = text;
    }
    s_.presents_ui = t.presents_ui_thread;
    s_.battery = BatteryState();
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

  xe::ui::Window* window_;
  xe::gpu::GraphicsSystem* graphics_;
  xe::Memory* memory_;
  Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
  Sample s_;
  clock::time_point last_sample_{};
};

std::unique_ptr<xe::ui::ImGuiDrawer> g_imgui;
std::unique_ptr<xe::ui::ImmediateDrawer> g_immediate;

// The Kinect Guide icon in the bottom-left corner, as the console showed it (Kinect Adventures' first
// start tells the player to look there): a ring that fills while the guide gesture's pose is held and
// is full while the guide is open. Made the first time the pose is held, so games without Kinect never
// draw it.
class GuideIndicator : public xe::ui::ImGuiDialog {
 public:
  explicit GuideIndicator(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const nui::GuideState guide = nui::GetGuideState();
    if (guide.progress <= 0 && !guide.open) return;
    using namespace theme;
    const float r = Px(26);
    const ImVec2 c(Px(40) + r, io.DisplaySize.y - Px(40) - r);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddCircleFilled(c, r + Px(7), color::Rgb(0x121417, 215), 48);
    draw->AddCircle(c, r, color::Rgb(0x3A3F46), 48, Px(4));
    constexpr float kPi = 3.14159265f;
    const float start = -kPi / 2, sweep = 2 * kPi * (guide.open ? 1.0f : guide.progress);
    draw->PathArcTo(c, r, start, start + sweep, 48);
    draw->PathStroke(guide.open ? color::kGreenBright : color::kAmber, 0, Px(4));
    // The gesture, seen from behind as the games draw it: the left arm out and down at 45 degrees.
    const ImU32 ink = color::Rgb(0xF2F2F2);
    const float t = Px(2.5f), k = r / 26.0f;
    auto p = [&](float x, float y) { return ImVec2(c.x + x * k, c.y + y * k); };
    draw->AddCircleFilled(p(0, -12), 4.5f * k, ink, 16);
    draw->AddLine(p(0, -7), p(0, 6), ink, t);                                    // body
    draw->AddLine(p(0, 6), p(-5, 17), ink, t), draw->AddLine(p(0, 6), p(5, 17), ink, t);  // legs
    draw->AddLine(p(-4, -5), p(-14, 5), ink, t);                                 // left arm, 45 degrees
    draw->AddLine(p(4, -5), p(5, 7), ink, t);                                    // right arm, down
  }
};
GuideIndicator* g_guide_indicator = nullptr;  // lives until exit
std::atomic<bool> g_guide_indicator_made{false};

StatsOverlay* g_overlay = nullptr;  // lives until exit; registered with the drawer only while shown
bool g_overlay_visible = false;

}  // namespace

void StartStatsOverlay(xe::ui::Window* window, xe::gpu::GraphicsSystem* graphics, xe::Memory* memory) {
  g_imgui = std::make_unique<xe::ui::ImGuiDrawer>(window, 1);
  theme::Setup(g_imgui.get());  // Inter and the dark theme, before the font texture is made
  g_immediate = graphics->provider()->CreateImmediateDrawer();
  g_imgui->SetPresenterAndImmediateDrawer(graphics->presenter(), g_immediate.get());
  // The overlay only shows text: draw it with new guest frames instead of at the monitor rate.
  g_imgui->SetContinuousRepaint(false);
  g_overlay = new StatsOverlay(g_imgui.get(), window, graphics, memory);  // registers itself
  g_imgui->RemoveDialog(g_overlay);
}

void SetStatsOverlayVisible(bool visible) {
  if (!g_overlay || visible == g_overlay_visible) return;
  g_overlay_visible = visible;
  if (visible)
    g_imgui->AddDialog(g_overlay);
  else
    g_imgui->RemoveDialog(g_overlay);
}

bool IsStatsOverlayVisible() { return g_overlay_visible; }

xe::ui::ImGuiDrawer* SharedImGuiDrawer() { return g_imgui.get(); }

xe::ui::ImmediateDrawer* SharedImmediateDrawer() { return g_immediate.get(); }

void NotifySwap(uint32_t frontbuffer_width, uint32_t frontbuffer_height) {
  g_frontbuffer_width = frontbuffer_width;
  g_frontbuffer_height = frontbuffer_height;
  timing::Record(timing::Event::kVdSwap);
  std::lock_guard lock(g_swap_mutex);
  auto now = clock::now();
  if (!g_swaps.empty()) {
    double gap = std::chrono::duration<double, std::milli>(now - g_swaps.back()).count();
    if (gap > 100.0) std::fprintf(stderr, "[gpu] no frame presented for %.0f ms\n", gap);
  }
  g_swaps.push_back(now);
  if (g_swaps.size() > 1000) g_swaps.pop_front();
  // The Kinect Guide icon, the first time its pose is held (the drawer belongs to the UI thread).
  if (g_imgui && !g_guide_indicator_made.load(std::memory_order_relaxed)) {
    const nui::GuideState guide = nui::GetGuideState();
    if ((guide.progress > 0 || guide.open) && !g_guide_indicator_made.exchange(true))
      PostToUIThread([] { g_guide_indicator = new GuideIndicator(g_imgui.get()); });  // registers itself
  }
}

}  // namespace x360::gpu
