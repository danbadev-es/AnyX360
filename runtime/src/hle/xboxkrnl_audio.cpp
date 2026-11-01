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
#include <cstring>
#include <thread>
#include <vector>

#include "x360/kernel.h"
#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/threading.h"

using namespace x360::kernel;
using xe::X_STATUS;  // Xenia's xbox.h defines the X_STATUS_* codes as casts to it

namespace {

constexpr size_t kMaximumQueuedFrames = 64;  // XAudio2 voice buffers (as Xenia's AudioSystem)
// Frames queued ahead of playback: 16 x 5.33 ms = 85 ms of latency, enough to absorb scheduling jitter.
constexpr size_t kQueuedFrames = 16;

struct AudioClient {
  uint32_t callback = 0;
  uint32_t argument = 0;  // guest pointer to a dword holding the client's argument
  bool active = false;
  std::unique_ptr<xe::threading::Semaphore> semaphore;
  std::unique_ptr<xe::apu::xaudio2::XAudio2AudioDriver> driver;
  std::chrono::steady_clock::time_point next_frame{};  // clients without host output
};

std::mutex g_audio_mutex;
AudioClient g_clients[8];
std::shared_ptr<Thread> g_audio_thread;
std::unique_ptr<xe::apu::XmaDecoder> g_xma;
std::atomic<uint32_t> g_xma_contexts{0};

// Logs gaps between mix callbacks long enough to drain the queued frames (heard as a cut).
std::atomic<int64_t> g_last_mix_us{0};
std::atomic<uint32_t> g_mixes{0};

int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void NoteMix() {
  int64_t now = NowUs();
  int64_t last = g_last_mix_us.exchange(now);
  double gap = (now - last) / 1000.0;
  if (++g_mixes > 100 && gap > 80.0) KLOG_W("audio: no frame mixed for %.0f ms (playback ran dry)", gap);
}

// ANYX360_AUDIO_WATCHDOG: dumps the guest threads when mixing stalls for 70 ms (a few times), to see
// what the mixer waits on.
void StartAudioWatchdog() {
  if (!std::getenv("ANYX360_AUDIO_WATCHDOG")) return;
  std::thread([] {
    int dumps = 0;
    bool in_stall = false;
    while (dumps < 6) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (g_mixes < 200) continue;
      bool stalled = NowUs() - g_last_mix_us.load() > 70000;
      if (stalled && !in_stall) {
        KLOG_W("audio watchdog: mixing stalled, thread dump %d", dumps + 1);
        DumpThreads();
        ++dumps;
      }
      in_stall = stalled;
    }
  }).detach();
}

// Produces one frame per buffer the host finishes playing: XAudio2 releases the client's semaphore
// at the end of each buffer, so the device's clock drives the title's mixer (a timer cannot: sleeps
// are only as fine as the system timer, ~15.6 ms, while a frame lasts 5.33 ms). Clients without host
// output run on a timer so the title's audio still advances.
void AudioThreadBody() {
  SetLatencySensitiveThread(THREAD_PRIORITY_ABOVE_NORMAL);
  using clock = std::chrono::steady_clock;
  const auto frame = std::chrono::microseconds(5333);  // 256 samples at 48 kHz
  for (;;) {
    std::vector<xe::threading::WaitHandle*> handles;
    std::vector<AudioClient*> owners;
    bool produced = false;
    for (AudioClient& c : g_clients) {
      uint32_t callback = 0, argument = 0;
      {
        std::lock_guard lock(g_audio_mutex);
        if (!c.active || !c.callback) continue;
        if (c.semaphore) {
          handles.push_back(c.semaphore.get());
          owners.push_back(&c);
          continue;
        }
        if (clock::now() < c.next_frame) continue;
        c.next_frame = std::max(c.next_frame + frame, clock::now() - 8 * frame);
        callback = c.callback;
        argument = c.argument;
      }
      CallGuest(callback, {argument});
      produced = true;
    }
    if (!handles.empty()) {
      auto [result, index] =
          xe::threading::WaitAny(handles.data(), handles.size(), false, std::chrono::milliseconds(2));
      if (result == xe::threading::WaitResult::kSuccess) {
        uint32_t callback, argument;
        {
          std::lock_guard lock(g_audio_mutex);
          if (!owners[index]->active) continue;
          callback = owners[index]->callback;
          argument = owners[index]->argument;
        }
        NoteMix();
        CallGuest(callback, {argument});
        continue;
      }
    } else if (!produced) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    c.semaphore->Release(kQueuedFrames, nullptr);
    c.driver = std::make_unique<xe::apu::xaudio2::XAudio2AudioDriver>(c.semaphore.get());
    if (!c.driver->Initialize()) {
      KLOG_W("XAudio2 output unavailable, client %u is silent", i);
      c.driver.reset();
      c.semaphore.reset();  // no pacing: produce frames on the timer and drop them
    }
    c.active = true;
    Store<uint32_t>(driver_ptr, 0x41550000 | i);
    if (!g_audio_thread) {
      g_audio_thread = CreateHostDrivenThread("audio", 0, AudioThreadBody);
      StartAudioWatchdog();
    }
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
  static auto window_start = std::chrono::steady_clock::now();
  if (++frames % 1000 == 0) {
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - window_start).count();
    window_start = now;
    KLOG_I("audio: %u frames, %.1f frames/s (48 kHz needs 187.5), peak %.3f, %u XMA contexts", frames,
           1000.0 / seconds, peak, g_xma_contexts.load());
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

// The categories whose volume the system changed since the last call (bit per category): the volumes
// never change here (all at 1.0).
uint32_t XAudioGetVoiceCategoryVolumeChangeMask(uint32_t driver, uint32_t mask_ptr) {
  (void)driver;
  if (mask_ptr) Store<uint32_t>(mask_ptr, 0);
  return X_STATUS_SUCCESS;
}

// The ducker lowers the title's audio under chat; with no chat, it stays off. The getters (level,
// threshold, attack, release, hold) all report 0.
uint32_t XAudioGetDuckerLevel(uint32_t level_ptr) {
  if (level_ptr) Store<uint32_t>(level_ptr, 0);
  return X_STATUS_SUCCESS;
}
uint32_t XAudioEnableDucker(uint32_t enable) {
  (void)enable;
  return X_STATUS_SUCCESS;
}
uint32_t XAudioOverrideSpeakerConfig(uint32_t config) {
  (void)config;
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

// The kernel's XMA context functions (as Xenia's): titles built with the older XAudio library
// (XAUD: Guitar Hero III's FMOD) drive their contexts through these as well as the registers.
// Context pointers are guest addresses of XMA_CONTEXT_DATA in the decoder's context array; buffer
// pointers in a context are physical addresses.
struct XmaLoopData {
  xe::be<uint32_t> loop_start;
  xe::be<uint32_t> loop_end;
  uint8_t loop_count;
  uint8_t loop_subframe_end;
  uint8_t loop_subframe_skip;
};
static_assert(sizeof(XmaLoopData) == 12);

struct XmaContextInit {
  xe::be<uint32_t> input_buffer_0_ptr;
  xe::be<uint32_t> input_buffer_0_packet_count;
  xe::be<uint32_t> input_buffer_1_ptr;
  xe::be<uint32_t> input_buffer_1_packet_count;
  xe::be<uint32_t> input_buffer_read_offset;
  xe::be<uint32_t> output_buffer_ptr;
  xe::be<uint32_t> output_buffer_block_count;
  xe::be<uint32_t> work_buffer;
  xe::be<uint32_t> subframe_decode_count;
  xe::be<uint32_t> channel_count;
  xe::be<uint32_t> sample_rate;
  XmaLoopData loop_data;
};
static_assert(sizeof(XmaContextInit) == 56);

// Sets the context's bit in one of the APU's per-context registers (0x1940 enable, 0x1A40 disable,
// 0x1A80 initialise), as the title's write to the hardware register would.
void StoreXmaContextRegister(uint32_t base_reg, uint32_t context) {
  uint32_t physical = GuestToPhysical(context);
  uint32_t index = (physical - g_xma->context_array_ptr()) / sizeof(xe::apu::XMA_CONTEXT_DATA);
  g_xma->WriteRegister(base_reg + (index >> 5) * 4, xe::byte_swap(uint32_t(1) << (index & 0x1F)));
}

template <typename F>
uint32_t UpdateXmaContext(uint32_t context, F&& update) {
  if (!context) return 0;
  xe::apu::XMA_CONTEXT_DATA data(Ptr<void>(context));
  update(data);
  data.Store(Ptr<void>(context));
  return 0;
}

template <typename F>
uint32_t ReadXmaContext(uint32_t context, F&& read) {
  if (!context) return 0;
  xe::apu::XMA_CONTEXT_DATA data(Ptr<void>(context));
  return read(data);
}

uint32_t XMAInitializeContext(uint32_t context, uint32_t init_ptr) {
  if (!g_xma || !context || !init_ptr) return 1;  // X_E_FALSE
  const auto& init = *Ptr<XmaContextInit>(init_ptr);
  uint32_t input_0 = init.input_buffer_0_ptr ? GuestToPhysical(init.input_buffer_0_ptr) : 0;
  uint32_t input_1 = init.input_buffer_1_ptr ? GuestToPhysical(init.input_buffer_1_ptr) : 0;
  uint32_t output = GuestToPhysical(init.output_buffer_ptr);
  std::memset(Ptr<void>(context), 0, sizeof(xe::apu::XMA_CONTEXT_DATA));
  xe::apu::XMA_CONTEXT_DATA data(Ptr<void>(context));
  data.input_buffer_0_ptr = input_0;
  data.input_buffer_0_packet_count = init.input_buffer_0_packet_count;
  data.input_buffer_1_ptr = input_1;
  data.input_buffer_1_packet_count = init.input_buffer_1_packet_count;
  data.input_buffer_read_offset = init.input_buffer_read_offset;
  data.output_buffer_ptr = output;
  data.output_buffer_block_count = init.output_buffer_block_count;
  data.subframe_decode_count = init.subframe_decode_count;
  data.is_stereo = init.channel_count >= 1;
  data.sample_rate = init.sample_rate;
  data.loop_start = init.loop_data.loop_start;
  data.loop_end = init.loop_data.loop_end;
  data.loop_count = init.loop_data.loop_count;
  data.loop_subframe_end = init.loop_data.loop_subframe_end;
  data.loop_subframe_skip = init.loop_data.loop_subframe_skip;
  data.Store(Ptr<void>(context));
  StoreXmaContextRegister(0x1A80, context);
  return 0;
}

uint32_t XMASetLoopData(uint32_t context, uint32_t loop_ptr) {
  const auto& loop = *Ptr<XmaLoopData>(loop_ptr);
  return UpdateXmaContext(context, [&](auto& d) {
    d.loop_start = loop.loop_start;
    d.loop_end = loop.loop_end;
    d.loop_count = loop.loop_count;
    d.loop_subframe_end = loop.loop_subframe_end;
    d.loop_subframe_skip = loop.loop_subframe_skip;
  });
}

uint32_t XMAGetInputBufferReadOffset(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.input_buffer_read_offset); });
}
uint32_t XMASetInputBufferReadOffset(uint32_t context, uint32_t value) {
  return UpdateXmaContext(context, [&](auto& d) { d.input_buffer_read_offset = value; });
}
uint32_t XMASetInputBuffer0(uint32_t context, uint32_t buffer, uint32_t packets) {
  return UpdateXmaContext(context, [&](auto& d) {
    d.input_buffer_0_ptr = GuestToPhysical(buffer);
    d.input_buffer_0_packet_count = packets;
  });
}
uint32_t XMASetInputBuffer1(uint32_t context, uint32_t buffer, uint32_t packets) {
  return UpdateXmaContext(context, [&](auto& d) {
    d.input_buffer_1_ptr = GuestToPhysical(buffer);
    d.input_buffer_1_packet_count = packets;
  });
}
uint32_t XMAIsInputBuffer0Valid(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.input_buffer_0_valid); });
}
uint32_t XMAIsInputBuffer1Valid(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.input_buffer_1_valid); });
}
uint32_t XMASetInputBuffer0Valid(uint32_t context) {
  return UpdateXmaContext(context, [](auto& d) { d.input_buffer_0_valid = 1; });
}
uint32_t XMASetInputBuffer1Valid(uint32_t context) {
  return UpdateXmaContext(context, [](auto& d) { d.input_buffer_1_valid = 1; });
}
uint32_t XMAIsOutputBufferValid(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.output_buffer_valid); });
}
uint32_t XMASetOutputBufferValid(uint32_t context) {
  return UpdateXmaContext(context, [](auto& d) { d.output_buffer_valid = 1; });
}
uint32_t XMAGetOutputBufferReadOffset(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.output_buffer_read_offset); });
}
uint32_t XMASetOutputBufferReadOffset(uint32_t context, uint32_t value) {
  return UpdateXmaContext(context, [&](auto& d) { d.output_buffer_read_offset = value; });
}
uint32_t XMAGetOutputBufferWriteOffset(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.output_buffer_write_offset); });
}
uint32_t XMAGetPacketMetadata(uint32_t context) {
  return ReadXmaContext(context, [](auto& d) { return uint32_t(d.packet_metadata); });
}
uint32_t XMAEnableContext(uint32_t context) {
  if (g_xma && context) StoreXmaContextRegister(0x1940, context);
  return 0;
}
uint32_t XMADisableContext(uint32_t context, uint32_t wait) {
  if (!g_xma || !context) return 0;
  StoreXmaContextRegister(0x1A40, context);
  return g_xma->BlockOnContext(context, !wait) ? 0 : 1;  // X_E_FALSE: still in use
}
uint32_t XMABlockWhileInUse(uint32_t context) {
  while (g_xma && context) {
    xe::apu::XMA_CONTEXT_DATA data(Ptr<void>(context));
    if (!data.input_buffer_0_valid && !data.input_buffer_1_valid) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return 0;
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
X360_EXPORT(XAudioGetVoiceCategoryVolumeChangeMask, XAudioGetVoiceCategoryVolumeChangeMask)
X360_EXPORT(XAudioGetDuckerLevel, XAudioGetDuckerLevel)
X360_EXPORT(XAudioGetDuckerThreshold, XAudioGetDuckerLevel)
X360_EXPORT(XAudioGetDuckerAttackTime, XAudioGetDuckerLevel)
X360_EXPORT(XAudioGetDuckerReleaseTime, XAudioGetDuckerLevel)
X360_EXPORT(XAudioGetDuckerHoldTime, XAudioGetDuckerLevel)
X360_EXPORT(XAudioEnableDucker, XAudioEnableDucker)
X360_EXPORT(XAudioOverrideSpeakerConfig, XAudioOverrideSpeakerConfig)
X360_EXPORT(XMACreateContext, XMACreateContext)
X360_EXPORT(XMAReleaseContext, XMAReleaseContext)
X360_EXPORT(XMAInitializeContext, XMAInitializeContext)
X360_EXPORT(XMASetLoopData, XMASetLoopData)
X360_EXPORT(XMAGetInputBufferReadOffset, XMAGetInputBufferReadOffset)
X360_EXPORT(XMASetInputBufferReadOffset, XMASetInputBufferReadOffset)
X360_EXPORT(XMASetInputBuffer0, XMASetInputBuffer0)
X360_EXPORT(XMASetInputBuffer1, XMASetInputBuffer1)
X360_EXPORT(XMAIsInputBuffer0Valid, XMAIsInputBuffer0Valid)
X360_EXPORT(XMAIsInputBuffer1Valid, XMAIsInputBuffer1Valid)
X360_EXPORT(XMASetInputBuffer0Valid, XMASetInputBuffer0Valid)
X360_EXPORT(XMASetInputBuffer1Valid, XMASetInputBuffer1Valid)
X360_EXPORT(XMAIsOutputBufferValid, XMAIsOutputBufferValid)
X360_EXPORT(XMASetOutputBufferValid, XMASetOutputBufferValid)
X360_EXPORT(XMAGetOutputBufferReadOffset, XMAGetOutputBufferReadOffset)
X360_EXPORT(XMASetOutputBufferReadOffset, XMASetOutputBufferReadOffset)
X360_EXPORT(XMAGetOutputBufferWriteOffset, XMAGetOutputBufferWriteOffset)
X360_EXPORT(XMAGetPacketMetadata, XMAGetPacketMetadata)
X360_EXPORT(XMAEnableContext, XMAEnableContext)
X360_EXPORT(XMADisableContext, XMADisableContext)
X360_EXPORT(XMABlockWhileInUse, XMABlockWhileInUse)
