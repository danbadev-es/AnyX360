// Glue between AnyX360's kernel and Xenia's GPU (D3D12) and UI.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <deque>

#include "x360/gpu.h"
#include "stats_overlay.h"
#include "x360/kernel.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/graphics_host.h"
#include "xenia/memory.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app_context_win.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

DECLARE_int32(log_level);

namespace x360::gpu {

using namespace x360::kernel;

namespace {

// Saves the latest guest frame as a 24-bit BMP (debugging aid, ANYX360_CAPTURE_DIR).
void CaptureFrame(xe::ui::Presenter* presenter, const std::string& dir, int index) {
  xe::ui::RawImage image;
  if (!presenter || !presenter->CaptureGuestOutput(image)) return;
  uint32_t row = (image.width * 3 + 3) & ~3u;
  uint32_t data_size = row * image.height;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
  put32(2, 54 + data_size);
  put32(10, 54);
  put32(14, 40);
  put32(18, image.width);
  put32(22, image.height);
  header[26] = 1;
  header[28] = 24;
  put32(34, data_size);
  char name[64];
  std::snprintf(name, sizeof(name), "/frame_%04d.bmp", index);
  FILE* f = std::fopen((dir + name).c_str(), "wb");
  if (!f) return;
  std::fwrite(header, 1, sizeof(header), f);
  std::vector<uint8_t> line(row, 0);
  for (uint32_t y = image.height; y-- > 0;) {
    const uint8_t* src = image.data.data() + y * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      line[x * 3 + 0] = src[x * 4 + 2];
      line[x * 3 + 1] = src[x * 4 + 1];
      line[x * 3 + 2] = src[x * 4 + 0];
    }
    std::fwrite(line.data(), 1, row, f);
  }
  std::fclose(f);
}

// Debugging aid: logs every place in guest memory holding `text` as ASCII or big-endian UTF-16
// (UE3 FStrings), with the surrounding characters.
void ScanGuestMemory(const char* text) {
  std::string ascii = text;
  std::string utf16;
  for (char c : ascii) utf16 += '\0', utf16 += c;
  if (ascii.rfind("0x", 0) == 0) {  // "0x12345678": a big-endian 32-bit value
    uint32_t value = uint32_t(std::strtoul(text + 2, nullptr, 16));
    ascii.assign({char(value >> 24), char(value >> 16), char(value >> 8), char(value)});
    utf16 = ascii;
  }
  uint8_t* base = XeMemory()->virtual_membase();
  uint8_t* at = base + 0x40000000ull;
  uint8_t* end = base + 0x100000000ull;
  int hits = 0;
  while (at < end && hits < 64) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(at, &info, sizeof(info))) break;
    uint8_t* region_end = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                    (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                                     PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY));
    if (readable) {
      std::string_view region(reinterpret_cast<const char*>(at), size_t(region_end - at));
      for (const std::string& needle : {ascii, utf16}) {
        bool wide = &needle == &utf16;
        for (size_t pos = region.find(needle); pos != std::string_view::npos && hits < 64;
             pos = region.find(needle, pos + 1)) {
          std::string context;
          size_t from = pos >= 64 ? pos - 64 : 0;
          for (size_t i = from; i < std::min(region.size(), pos + 160); i += wide ? 2 : 1) {
            char c = region[wide ? i + 1 : i];
            if (wide && region[i] != 0) c = '?';
            context += (c >= 32 && c < 127) ? c : '.';
          }
          KLOG_I("memscan %s %08X: %s", wide ? "w" : "a", uint32_t(at + pos - base), context.c_str());
          if (std::getenv("ANYX360_MEMSCAN_HEX") && pos >= 32) {
            std::string hex;
            char byte[4];
            for (size_t i = pos - 32; i < pos; ++i) {
              std::snprintf(byte, sizeof(byte), "%02X ", uint8_t(region[i]));
              hex += byte;
            }
            KLOG_I("memscan   before: %s", hex.c_str());
          }
          ++hits;
        }
      }
    }
    at = region_end;
  }
}

struct InterruptRequest {
  uint32_t callback;
  uint32_t data;
  uint32_t source;
};

std::unique_ptr<xe::ui::Win32WindowedAppContext> g_app;
std::unique_ptr<xe::ui::Window> g_window;
std::unique_ptr<xe::gpu::d3d12::D3D12GraphicsSystem> g_graphics;
std::shared_ptr<Thread> g_interrupt_thread;
std::mutex g_interrupt_mutex;
std::condition_variable g_interrupt_cv;
std::deque<InterruptRequest> g_interrupts;
std::atomic<int> g_exit_code{0};
std::atomic<uint32_t> g_writeback{0};

}  // namespace

std::atomic<uint64_t> g_swap_count{0};
std::atomic<uint64_t> g_interrupt_count{0};

namespace {

// Runs the title's interrupt callback the way the kernel's DPC does: on a guest thread at
// DISPATCH_LEVEL with the PRCB's DPC-active flag set.
void RunInterrupt(const InterruptRequest& r) {
  ++g_interrupt_count;
  Thread* t = CurrentThread();
  Store<uint8_t>(t->pcr + 0x18, 2);
  Store<uint32_t>(t->pcr + 0x150, 1);
  CallGuest(r.callback, {r.source, r.data});
  Store<uint32_t>(t->pcr + 0x150, 0);
  Store<uint8_t>(t->pcr + 0x18, 0);
}

class Host final : public xe::gpu::GraphicsHost {
 public:
  void DispatchInterrupt(uint32_t callback, uint32_t user_data, uint32_t source, uint32_t cpu) override {
    (void)cpu;
    InterruptRequest r{callback, user_data, source};
    if (g_interrupt_thread && CurrentThread() == g_interrupt_thread.get()) {
      RunInterrupt(r);
      return;
    }
    std::lock_guard lock(g_interrupt_mutex);
    g_interrupts.push_back(r);
    g_interrupt_cv.notify_all();
  }
  uint32_t GetTitleId() override { return Title().title_id; }
};

Host g_host;

// vblank at 60 Hz plus queued command-buffer interrupts, delivered on a guest thread.
void InterruptThreadBody() {
  using clock = std::chrono::steady_clock;
  const auto period = std::chrono::microseconds(16667);
  auto next_vblank = clock::now() + period;
  for (;;) {
    InterruptRequest request{};
    bool have_request = false;
    {
      std::unique_lock lock(g_interrupt_mutex);
      g_interrupt_cv.wait_until(lock, next_vblank, [] { return !g_interrupts.empty(); });
      if (!g_interrupts.empty()) {
        request = g_interrupts.front();
        g_interrupts.pop_front();
        have_request = true;
      }
    }
    if (have_request) {
      RunInterrupt(request);
      continue;
    }
    next_vblank += period;
    if (next_vblank < clock::now()) next_vblank = clock::now() + period;
    g_graphics->MarkVblank();  // calls DispatchInterrupt(source 0) back on this thread
  }
}

class WindowListener final : public xe::ui::WindowListener {
 public:
  void OnClosing(xe::ui::UIEvent& e) override {
    (void)e;
    KLOG_I("window closed: exiting");
    std::fflush(nullptr);
    std::_Exit(0);
  }
};
WindowListener g_window_listener;

}  // namespace

bool Initialize(const char* window_title) {
  // ANYX360_XENIA_LOG_LEVEL: 0 error, 1 warning, 2 info (default), 3 debug (GPU details).
  if (const char* level = std::getenv("ANYX360_XENIA_LOG_LEVEL")) cvars::log_level = std::atoi(level);
  KLOG_I("GPU init: logging");
  xe::InitializeLogging("AnyX360");
  KLOG_I("GPU init: UI context");

  g_app = std::make_unique<xe::ui::Win32WindowedAppContext>(GetModuleHandleW(nullptr), SW_SHOWNORMAL);
  if (!g_app->Initialize()) {
    KLOG_E("could not initialize the UI");
    return false;
  }
  KLOG_I("GPU init: window");
  g_window = xe::ui::Window::Create(*g_app, window_title, 1280, 720);
  if (!g_window) {
    KLOG_E("could not create the game window");
    return false;
  }
  g_window->AddListener(&g_window_listener);
  if (!g_window->Open()) {
    KLOG_E("could not open the game window");
    return false;
  }

  if (!xe::gpu::d3d12::D3D12GraphicsSystem::IsAvailable()) {
    KLOG_E("Direct3D 12 is not available on this system");
    return false;
  }
  // Occlusion query mode (fake, fast, fast-alt, strict); see Xenia's occlusion_query cvar.
  if (const char* zpd = std::getenv("ANYX360_ZPD")) xe::gpu::SetZPDMode(zpd);
  KLOG_I("GPU init: D3D12 graphics system");
  g_graphics = std::make_unique<xe::gpu::d3d12::D3D12GraphicsSystem>();
  if (g_graphics->Setup(XeMemory(), &g_host, g_app.get(), true) != 0) {
    KLOG_E("could not initialize the Direct3D 12 GPU back end");
    return false;
  }
  KLOG_I("GPU init: presenter");
  g_window->SetPresenter(g_graphics->presenter());
  StartStatsOverlay(g_window.get(), g_graphics.get(), XeMemory());

  g_interrupt_thread = CreateHostDrivenThread("GPU interrupts", 2, InterruptThreadBody);
  std::thread([] {
    SetThreadDescription(GetCurrentThread(), L"GPU stats");
    uint64_t last_swaps = 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      uint64_t swaps = g_swap_count.load();
      uint32_t wptr = g_graphics->register_file()->values[0x01C5];
      uint32_t wb = g_writeback.load();
      uint32_t rptr = wb ? load_be<uint32_t>(XeMemory()->TranslatePhysical(wb)) : 0;
      KLOG_I("GPU: %llu swaps (+%llu), %llu interrupts, wptr %u, rptr %u", (unsigned long long)swaps,
             (unsigned long long)(swaps - last_swaps), (unsigned long long)g_interrupt_count.load(), wptr, rptr);
      static int stalled = 0;
      stalled = (swaps == last_swaps) ? stalled + 1 : 0;
      if (stalled == 2) DumpThreads();
      last_swaps = swaps;
      static const char* capture_dir = std::getenv("ANYX360_CAPTURE_DIR");
      static int capture_index = 0;
      if (capture_dir) CaptureFrame(g_graphics->presenter(), capture_dir, capture_index++);
      static const char* dump_at = std::getenv("ANYX360_DUMP_AT");  // seconds, comma separated
      static int tick = 0;
      ++tick;
      if (dump_at) {
        for (const char* p = dump_at; *p;) {
          int seconds = std::atoi(p);
          if (seconds / 5 == tick) {
            DumpThreads();
            if (const char* text = std::getenv("ANYX360_MEMSCAN")) ScanGuestMemory(text);
            if (const char* dump = std::getenv("ANYX360_MEMDUMP")) {  // "addr,length" in hex
              uint32_t addr = uint32_t(std::strtoul(dump, nullptr, 16));
              const char* comma = std::strchr(dump, ',');
              uint32_t length = comma ? uint32_t(std::strtoul(comma + 1, nullptr, 16)) : 0x100;
              for (uint32_t row = 0; row < length; row += 32) {
                std::string line;
                char word[12];
                for (uint32_t i = 0; i < 32; i += 4) {
                  std::snprintf(word, sizeof(word), "%08X ", Load<uint32_t>(addr + row + i));
                  line += word;
                }
                KLOG_I("memdump %08X: %s", addr + row, line.c_str());
              }
            }
          }
          while (*p && *p != ',') ++p;
          if (*p == ',') ++p;
        }
      }
    }
  }).detach();
  KLOG_I("GPU: %s", g_graphics->name().c_str());
  return true;
}

void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id) {
  if (!g_graphics) return;
  KLOG_I("GPU: shader storage in %s", cache_root.string().c_str());
  g_graphics->InitializeShaderStorage(cache_root, title_id, false);
}

int RunMessageLoop() {
  int result = g_app->RunMainMessageLoop();
  return result ? result : g_exit_code.load();
}

void RequestQuit(int exit_code) {
  g_exit_code = exit_code;
  g_app->CallInUIThread([] { g_app->QuitFromUIThread(); });
}

void InitializeRingBuffer(uint32_t physical, uint32_t size_log2) {
  g_graphics->InitializeRingBuffer(physical, size_log2);
}

void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2) {
  g_writeback = physical;
  g_graphics->EnableReadPointerWriteBack(physical, block_size_log2);
}

void SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  KLOG_I("GPU: interrupt callback %08X (%08X)", callback, user_data);
  g_graphics->SetInterruptCallback(callback, user_data);
}

}  // namespace x360::gpu

// Xenia's config.cc (persisted settings) is not part of the subset AnyX360 builds.
namespace config {
void SaveConfig() {}
}  // namespace config
