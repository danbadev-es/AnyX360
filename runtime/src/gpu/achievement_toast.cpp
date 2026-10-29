// The toast an unlocked achievement shows (x360/gpu.h ShowAchievementToast), like the console's: a
// dark pill at the bottom centre of the window with the achievement's picture, "Achievement
// unlocked" and "<gamerscore>G - <name>", rising in, held five seconds, then fading out, with a short
// chime. Several in a row show one after another. settings.toml's [display] achievement_toasts and
// achievement_sound (F2 > Display > Overlay) turn them off.
//
// The system's notifications (ShowNotification: a controller connected or disconnected, a profile
// signed in or out) share the queue: the same pill with a letter or number in the circle and no
// chime (system_notifications turns them off).
//
// It is drawn on the foreground of the shared ImGui drawer without a window, so it never takes the
// game's mouse or keyboard; it grows with the window as the sign-in picker does.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gpu_internal.h"
#include "stats_overlay.h"
#include "ui_theme.h"
#include "third_party/imgui/imgui.h"
#include "third_party/stb/stb_image.h"  // its implementation is compiled into Xenia's ImGui drawer
#include "x360/gpu.h"
#include "x360/kernel.h"
#include "x360/settings.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

namespace x360::gpu {

namespace {

using namespace theme;
using Clock = std::chrono::steady_clock;

constexpr double kIn = 0.35, kHold = 5.0, kOut = 0.45;

const char* UnlockedText() { return kernel::TitlePaths().language == 5 ? "Logro desbloqueado" : "Achievement unlocked"; }

// The chime: two soft notes (a fifth apart), 16-bit mono WAV in memory, made once.
const std::vector<uint8_t>& Chime() {
  static const std::vector<uint8_t> wav = [] {
    constexpr uint32_t kRate = 44100;
    constexpr double kLength = 0.55;
    const uint32_t samples = uint32_t(kRate * kLength);
    std::vector<int16_t> pcm(samples);
    constexpr double kPi = 3.14159265358979;
    for (uint32_t i = 0; i < samples; ++i) {
      const double t = double(i) / kRate;
      double v = 0;
      auto note = [&](double start, double hz, double gain) {
        if (t < start) return;
        const double u = t - start;
        const double envelope = std::min(1.0, u / 0.006) * std::exp(-u * 7.5);
        v += gain * envelope * (std::sin(2 * kPi * hz * u) + 0.25 * std::sin(4 * kPi * hz * u));
      };
      note(0.0, 987.77, 0.55);   // B5
      note(0.09, 1479.98, 0.5);  // F#6
      pcm[i] = int16_t(std::clamp(v * 0.32, -1.0, 1.0) * 32767);
    }
    std::vector<uint8_t> out(44 + samples * 2);
    auto put32 = [&](size_t at, uint32_t x) { std::memcpy(&out[at], &x, 4); };
    auto put16 = [&](size_t at, uint16_t x) { std::memcpy(&out[at], &x, 2); };
    std::memcpy(&out[0], "RIFF", 4), put32(4, uint32_t(out.size() - 8)), std::memcpy(&out[8], "WAVEfmt ", 8);
    put32(16, 16), put16(20, 1), put16(22, 1), put32(24, kRate), put32(28, kRate * 2), put16(32, 2), put16(34, 16);
    std::memcpy(&out[36], "data", 4), put32(40, samples * 2);
    std::memcpy(&out[44], pcm.data(), samples * 2);
    return out;
  }();
  return wav;
}

struct Toast {
  std::string title;  // "Achievement unlocked" (and the gamertag, with several players)
  std::string text;   // "20G - Name"
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;  // the picture, decoded
  std::unique_ptr<xe::ui::ImmediateTexture> texture;
  char glyph = 0;  // a notification: this in the circle, no chime
  Clock::time_point start{};
  bool started = false;
};

std::mutex g_mutex;
std::deque<std::shared_ptr<Toast>> g_queue;  // the first is shown
std::atomic<bool> g_active{false};           // a toast is queued or shown: the drawer paints

class ToastDialog : public xe::ui::ImGuiDialog {
 public:
  explicit ToastDialog(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::shared_ptr<Toast> toast;
    {
      std::lock_guard lock(g_mutex);
      if (g_queue.empty()) {
        g_active = false;
        imgui_drawer()->RemoveDialog(this);
        return;
      }
      toast = g_queue.front();
    }
    const Clock::time_point now = Clock::now();
    if (!toast->started) {
      toast->started = true;
      toast->start = now;
      if (!toast->glyph && settings::Get().achievement_sound)
        PlaySoundW(reinterpret_cast<LPCWSTR>(Chime().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
    const double t = std::chrono::duration<double>(now - toast->start).count();
    if (t >= kIn + kHold + kOut) {
      std::lock_guard lock(g_mutex);
      if (!g_queue.empty() && g_queue.front() == toast) g_queue.pop_front();
      return;
    }
    if (!toast->texture && !toast->rgba.empty()) {
      if (xe::ui::ImmediateDrawer* immediate = SharedImmediateDrawer())
        toast->texture = immediate->CreateTexture(toast->width, toast->height, xe::ui::ImmediateTextureFilter::kLinear,
                                                  false, toast->rgba.data());
      toast->rgba.clear();
    }
    Draw(io, *toast, t);
  }

 private:
  void Draw(ImGuiIO& io, const Toast& toast, double t) {
    const ImVec2 display = io.DisplaySize;
    const float scale = std::clamp(display.y / (720.0f * Scale()), 1.0f, 3.0f);
    auto S = [&](float dips) { return Px(dips) * scale; };
    // In: rising and appearing; out: fading.
    const float in = float(std::clamp(t / kIn, 0.0, 1.0));
    const float ease = 1 - (1 - in) * (1 - in) * (1 - in);
    const float alpha = t > kIn + kHold ? float(1 - (t - kIn - kHold) / kOut) : ease;
    auto with = [&](ImU32 color, float a = 1) {
      const unsigned base = (color >> IM_COL32_A_SHIFT) & 0xFF;
      return (color & ~IM_COL32_A_MASK) | (ImU32(std::clamp(base * alpha * a, 0.0f, 255.0f)) << IM_COL32_A_SHIFT);
    };

    ImFont* small_font = Small();
    ImFont* heading = Heading();
    const float small_size = small_font->FontSize * scale, heading_size = heading->FontSize * scale;
    const float h = S(64), icon = S(48), pad = (h - icon) / 2;
    // The name, shortened with an ellipsis to fit at most half the window.
    const float max_text = std::max(S(160), display.x * 0.5f);
    std::string text = toast.text;
    auto width_of = [&](ImFont* f, float size, const std::string& s) {
      return f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x;
    };
    if (width_of(heading, heading_size, text) > max_text) {
      while (!text.empty() && width_of(heading, heading_size, text + "...") > max_text) {
        text.pop_back();
        while (!text.empty() && (uint8_t(text.back()) & 0xC0) == 0x80) text.pop_back();  // a whole UTF-8 character
      }
      text += "...";
    }
    const float text_w = std::max(width_of(heading, heading_size, text), width_of(small_font, small_size, toast.title));
    const float w = pad + icon + S(16) + text_w + S(28);
    const float x = std::round((display.x - w) / 2);
    const float y = std::round(display.y - S(56) - h + (1 - ease) * S(24));

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(ImVec2(x + S(2), y + S(4)), ImVec2(x + w + S(2), y + h + S(4)), with(color::Rgb(0x000000, 90)), h / 2);
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), with(color::Rgb(0x1C1F23, 242)), h / 2);
    draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), with(color::Rgb(0x3A3F46)), h / 2, 0, S(1));
    // The picture in a circle with a green ring; the trophy's "G" without one.
    const ImVec2 c(x + pad + icon / 2, y + h / 2);
    if (toast.texture) {
      draw->AddImageRounded(reinterpret_cast<ImTextureID>(toast.texture.get()), ImVec2(c.x - icon / 2, c.y - icon / 2),
                            ImVec2(c.x + icon / 2, c.y + icon / 2), ImVec2(0, 0), ImVec2(1, 1), with(IM_COL32_WHITE),
                            icon / 2);
    } else {
      draw->AddCircleFilled(c, icon / 2, with(toast.glyph ? color::Rgb(0x30353B) : color::kGreen), 48);
      const char glyph[2] = {toast.glyph ? toast.glyph : 'G', 0};
      const ImVec2 g = heading->CalcTextSizeA(heading_size * 1.4f, FLT_MAX, 0, glyph);
      draw->AddText(heading, heading_size * 1.4f, ImVec2(c.x - g.x / 2, c.y - g.y / 2), with(IM_COL32_WHITE), glyph);
    }
    if (!toast.glyph) draw->AddCircle(c, icon / 2 + S(1.5f), with(color::kGreenBright), 48, S(2.5f));
    const float tx = x + pad + icon + S(16);
    const float block = small_size + S(3) + heading_size;
    const float ty = y + (h - block) / 2;
    draw->AddText(small_font, small_size, ImVec2(tx, ty), with(color::Rgb(0xB8BEC6)), toast.title.c_str());
    draw->AddText(heading, heading_size, ImVec2(tx, ty + small_size + S(3)), with(color::Rgb(0xFFFFFF)), text.c_str());
  }
};

ToastDialog* g_dialog = nullptr;  // lives until exit; registered with the drawer while a toast shows

// The drawer paints with new guest frames only: while a toast shows, it is asked to paint at 60 Hz
// (a game paused or loading may present nothing for a while).
void PaintThread() {
  SetThreadDescription(GetCurrentThread(), L"Achievement toast");
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    if (!g_active) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    PostToUIThread([] {
      if (auto* drawer = SharedImGuiDrawer()) drawer->RequestPaintIfIdle(0.016);
    });
  }
}

void Queue(std::shared_ptr<Toast> toast) {
  static std::once_flag started;
  std::call_once(started, [] { std::thread(PaintThread).detach(); });
  {
    std::lock_guard lock(g_mutex);
    g_queue.push_back(std::move(toast));
    g_active = true;
  }
  PostToUIThread([] {
    xe::ui::ImGuiDrawer* drawer = SharedImGuiDrawer();
    if (!g_dialog) {
      g_dialog = new ToastDialog(drawer);  // registers itself
      drawer->RemoveDialog(g_dialog);
    }
    drawer->AddDialog(g_dialog);  // once: already there while toasts show
    drawer->RequestPaintIfIdle(0);
  });
}

}  // namespace

bool ShowNotification(const Notification& notification) {
  if (!settings::Get().system_notifications || !SharedImGuiDrawer() || !GameWindow()) return false;
  auto toast = std::make_shared<Toast>();
  toast->title = notification.title;
  toast->text = notification.text;
  toast->glyph = notification.glyph ? char(std::toupper(uint8_t(notification.glyph))) : '!';
  KLOG_I("notification: %s: %s", notification.title.c_str(), notification.text.c_str());
  Queue(std::move(toast));
  return true;
}

bool ShowAchievementToast(const AchievementToast& unlocked) {
  if (!settings::Get().achievement_toasts || !SharedImGuiDrawer() || !GameWindow()) return false;
  auto toast = std::make_shared<Toast>();
  toast->title = UnlockedText();
  if (!unlocked.gamertag.empty()) toast->title += " \xC2\xB7 " + unlocked.gamertag;
  toast->text = std::to_string(unlocked.gamerscore) + "G - " + unlocked.name;
  if (!unlocked.png.empty()) {
    int w = 0, h = 0, channels = 0;
    if (stbi_uc* pixels = stbi_load_from_memory(unlocked.png.data(), int(unlocked.png.size()), &w, &h, &channels, 4)) {
      toast->width = uint32_t(w), toast->height = uint32_t(h);
      toast->rgba.assign(pixels, pixels + size_t(w) * h * 4);
      stbi_image_free(pixels);
    }
  }
  Queue(std::move(toast));
  return true;
}

}  // namespace x360::gpu
