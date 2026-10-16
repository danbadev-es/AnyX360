// Glue between AnyX360's kernel and Xenia's GPU (D3D12) and UI.
#include <chrono>
#include <deque>

#include "x360/gpu.h"
#include "x360/kernel.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/graphics_host.h"
#include "xenia/memory.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app_context_win.h"

DECLARE_int32(log_level);

namespace x360::gpu {

using namespace x360::kernel;

namespace {

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
  KLOG_I("GPU init: D3D12 graphics system");
  g_graphics = std::make_unique<xe::gpu::d3d12::D3D12GraphicsSystem>();
  if (g_graphics->Setup(XeMemory(), &g_host, g_app.get(), true) != 0) {
    KLOG_E("could not initialize the Direct3D 12 GPU back end");
    return false;
  }
  KLOG_I("GPU init: presenter");
  g_window->SetPresenter(g_graphics->presenter());

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
    }
  }).detach();
  KLOG_I("GPU: %s", g_graphics->name().c_str());
  return true;
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
