// xboxkrnl audio exports. XAudio2 inside the title renders through a "render driver client": the
// kernel calls the client back once per 256-sample frame (48 kHz) and the client submits the mixed
// frame. Frames are discarded for now (silence), but the callbacks keep the title's audio engine
// running as it would on the console.
#include <chrono>

#include "x360/kernel.h"

using namespace x360::kernel;

namespace {

struct AudioClient {
  uint32_t callback = 0;
  uint32_t argument = 0;
  bool active = false;
};

std::mutex g_audio_mutex;
AudioClient g_clients[8];
std::shared_ptr<Thread> g_audio_thread;

void AudioThreadBody() {
  using clock = std::chrono::steady_clock;
  const auto frame = std::chrono::microseconds(5333);  // 256 samples at 48 kHz
  auto next = clock::now();
  for (;;) {
    std::this_thread::sleep_until(next);
    next += frame;
    if (next < clock::now()) next = clock::now() + frame;
    AudioClient clients[8];
    {
      std::lock_guard lock(g_audio_mutex);
      std::copy(std::begin(g_clients), std::end(g_clients), clients);
    }
    for (const AudioClient& c : clients) {
      if (c.active && c.callback) CallGuest(c.callback, {c.argument});
    }
  }
}

uint32_t XAudioRegisterRenderDriverClient(uint32_t callback_ptr, uint32_t driver_ptr) {
  uint32_t callback = Load<uint32_t>(callback_ptr);
  uint32_t argument = Load<uint32_t>(callback_ptr + 4);
  std::lock_guard lock(g_audio_mutex);
  for (uint32_t i = 0; i < 8; ++i) {
    if (g_clients[i].active) continue;
    g_clients[i] = {callback, argument, true};
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
  if (i < 8) g_clients[i].active = false;
  return X_STATUS_SUCCESS;
}

uint32_t XAudioSubmitRenderDriverFrame(uint32_t driver, uint32_t samples) {
  (void)driver, (void)samples;  // 6 channels x 256 big-endian floats; discarded for now
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

// XMA contexts live in physical memory and are driven through APU registers by the title. The
// hardware decoder is not emulated yet: contexts are allocated but never produce samples.
uint32_t XMACreateContext(uint32_t context_ptr) {
  uint32_t context = PhysicalAlloc(64, 0x1000, 256, 0, 0);
  Store<uint32_t>(context_ptr, context);
  return context ? X_STATUS_SUCCESS : X_STATUS_NO_MEMORY;
}

uint32_t XMAReleaseContext(uint32_t context) {
  (void)context;
  return X_STATUS_SUCCESS;
}

}  // namespace

namespace x360::kernel {
void InitAudio() {}
}  // namespace x360::kernel

X360_EXPORT(XAudioRegisterRenderDriverClient, XAudioRegisterRenderDriverClient)
X360_EXPORT(XAudioUnregisterRenderDriverClient, XAudioUnregisterRenderDriverClient)
X360_EXPORT(XAudioSubmitRenderDriverFrame, XAudioSubmitRenderDriverFrame)
X360_EXPORT(XAudioGetSpeakerConfig, XAudioGetSpeakerConfig)
X360_EXPORT(XAudioGetVoiceCategoryVolume, XAudioGetVoiceCategoryVolume)
X360_EXPORT(XAudioGetDuckerLevel, XAudioGetDuckerLevel)
X360_EXPORT(XMACreateContext, XMACreateContext)
X360_EXPORT(XMAReleaseContext, XMAReleaseContext)
