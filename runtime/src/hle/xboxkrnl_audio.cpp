// xboxkrnl audio exports. XAudio2 inside the title renders through a "render driver client": the
// kernel calls the client back once per 256-sample frame (48 kHz, 6 channels) and the client
// submits the mixed frame, which goes to the host through Xenia's XAudio2 driver. The callback only
// runs while the host has a free output buffer, so the host's playback clock paces the title.
//
// Compressed audio is XMA, decoded by the console's audio hardware: Xenia's XmaDecoder emulates it
// (contexts in guest memory, APU registers at 0x7FEA0000 handled through MMIO).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "x360/kernel.h"
#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/threading.h"

using namespace x360::kernel;
using xe::X_STATUS;  // Xenia's xbox.h defines the X_STATUS_* codes as casts to it

namespace {

constexpr size_t kMaximumQueuedFrames = 64;  // as Xenia's AudioSystem

struct AudioClient {
  uint32_t callback = 0;
  uint32_t argument = 0;  // guest pointer to a dword holding the client's argument
  bool active = false;
  std::unique_ptr<xe::threading::Semaphore> semaphore;
  std::unique_ptr<xe::apu::xaudio2::XAudio2AudioDriver> driver;
};

std::mutex g_audio_mutex;
AudioClient g_clients[8];
std::shared_ptr<Thread> g_audio_thread;
std::unique_ptr<xe::apu::XmaDecoder> g_xma;
std::atomic<uint32_t> g_xma_contexts{0};

void AudioThreadBody() {
  using clock = std::chrono::steady_clock;
  const auto frame = std::chrono::microseconds(5333);  // 256 samples at 48 kHz
  auto next = clock::now();
  for (;;) {
    std::this_thread::sleep_until(next);
    next += frame;
    if (next < clock::now()) next = clock::now() + frame;
    for (AudioClient& c : g_clients) {
      uint32_t callback, argument;
      {
        std::lock_guard lock(g_audio_mutex);
        if (!c.active || !c.callback) continue;
        // Only produce a frame when the host has room for it.
        if (c.semaphore && xe::threading::Wait(c.semaphore.get(), false, std::chrono::milliseconds(0)) !=
                               xe::threading::WaitResult::kSuccess)
          continue;
        callback = c.callback;
        argument = c.argument;
      }
      CallGuest(callback, {argument});
    }
  }
}

uint32_t XAudioRegisterRenderDriverClient(uint32_t callback_ptr, uint32_t driver_ptr) {
  uint32_t callback = Load<uint32_t>(callback_ptr);
  uint32_t argument = Load<uint32_t>(callback_ptr + 4);
  std::lock_guard lock(g_audio_mutex);
  for (uint32_t i = 0; i < 8; ++i) {
    AudioClient& c = g_clients[i];
    if (c.active) continue;
    // The callback receives a pointer to a dword holding its argument (as the console's
    // audio driver does), not the argument itself.
    uint32_t wrapped = SystemAlloc(4);
    Store<uint32_t>(wrapped, argument);
    c.callback = callback;
    c.argument = wrapped;
    c.semaphore = xe::threading::Semaphore::Create(0, kMaximumQueuedFrames);
    c.semaphore->Release(kMaximumQueuedFrames, nullptr);
    c.driver = std::make_unique<xe::apu::xaudio2::XAudio2AudioDriver>(c.semaphore.get());
    if (!c.driver->Initialize()) {
      KLOG_W("XAudio2 output unavailable, client %u is silent", i);
      c.driver.reset();
      c.semaphore.reset();  // no pacing: produce frames on the timer and drop them
    }
    c.active = true;
    Store<uint32_t>(driver_ptr, 0x41550000 | i);
    if (!g_audio_thread) g_audio_thread = CreateHostDrivenThread("audio", 0, AudioThreadBody);
    KLOG_I("XAudioRegisterRenderDriverClient: callback %08X(%08X) -> client %u", callback, argument, i);
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_NO_MEMORY;
}

uint32_t XAudioUnregisterRenderDriverClient(uint32_t driver) {
  std::lock_guard lock(g_audio_mutex);
  uint32_t i = driver & 0xFFFF;
  if (i < 8) {
    AudioClient& c = g_clients[i];
    c.active = false;
    if (c.driver) c.driver->Shutdown();
    c.driver.reset();
    c.semaphore.reset();
  }
  return X_STATUS_SUCCESS;
}

// samples: 6 channels x 256 big-endian floats, planar.
uint32_t XAudioSubmitRenderDriverFrame(uint32_t driver, uint32_t samples) {
  uint32_t i = driver & 0xFFFF;
  std::lock_guard lock(g_audio_mutex);
  if (i < 8 && g_clients[i].active && g_clients[i].driver && samples)
    g_clients[i].driver->SubmitFrame(Ptr<float>(samples));
  // Peak level over ~5 s of frames, for diagnostics.
  static float peak = 0;
  static uint32_t frames = 0;
  if (samples) {
    for (uint32_t s = 0; s < 6 * 256; ++s) {
      uint32_t bits = Load<uint32_t>(samples + s * 4);
      float v;
      std::memcpy(&v, &bits, 4);
      peak = std::max(peak, std::fabs(v));
    }
  }
  if (++frames % 1000 == 0) {
    KLOG_I("audio: %u frames, peak %.3f, %u XMA contexts", frames, peak, g_xma_contexts.load());
    peak = 0;
  }
  return X_STATUS_SUCCESS;
}

uint32_t XAudioGetSpeakerConfig(uint32_t config_ptr) {
  Store<uint32_t>(config_ptr, 0x00010001);
  return X_STATUS_SUCCESS;
}

uint32_t XAudioGetVoiceCategoryVolume(uint32_t category, uint32_t volume_ptr) {
  (void)category;
  float one = 1.0f;
  uint32_t bits;
  std::memcpy(&bits, &one, 4);
  Store<uint32_t>(volume_ptr, bits);
  return X_STATUS_SUCCESS;
}

uint32_t XAudioGetDuckerLevel(uint32_t level_ptr) {
  if (level_ptr) Store<uint32_t>(level_ptr, 0);
  return X_STATUS_SUCCESS;
}

// XMA contexts live in the decoder's context array (physical memory); the title drives them
// through the APU registers.
uint32_t XMACreateContext(uint32_t context_ptr) {
  uint32_t context = g_xma ? g_xma->AllocateContext() : 0;
  if (context) ++g_xma_contexts;
  Store<uint32_t>(context_ptr, context);
  return context ? X_STATUS_SUCCESS : X_STATUS_NO_MEMORY;
}

uint32_t XMAReleaseContext(uint32_t context) {
  if (g_xma && context) {
    g_xma->ReleaseContext(context);
    --g_xma_contexts;
  }
  return X_STATUS_SUCCESS;
}

}  // namespace

namespace x360::kernel {
void InitAudio() {
  g_xma = std::make_unique<xe::apu::XmaDecoder>(XeMemory());
  if (g_xma->Setup() != X_STATUS_SUCCESS) {
    KLOG_E("XMA decoder setup failed: audio stays silent");
    g_xma.reset();
  }
}
}  // namespace x360::kernel

X360_EXPORT(XAudioRegisterRenderDriverClient, XAudioRegisterRenderDriverClient)
X360_EXPORT(XAudioUnregisterRenderDriverClient, XAudioUnregisterRenderDriverClient)
X360_EXPORT(XAudioSubmitRenderDriverFrame, XAudioSubmitRenderDriverFrame)
X360_EXPORT(XAudioGetSpeakerConfig, XAudioGetSpeakerConfig)
X360_EXPORT(XAudioGetVoiceCategoryVolume, XAudioGetVoiceCategoryVolume)
X360_EXPORT(XAudioGetDuckerLevel, XAudioGetDuckerLevel)
X360_EXPORT(XMACreateContext, XMACreateContext)
X360_EXPORT(XMAReleaseContext, XMAReleaseContext)
