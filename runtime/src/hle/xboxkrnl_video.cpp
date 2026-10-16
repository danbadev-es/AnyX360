// xboxkrnl video (Vd*) exports, on top of the PM4 command processor (runtime/src/gpu).
#include "x360/gpu.h"
#include "x360/kernel.h"

using namespace x360::kernel;
namespace gpu = x360::gpu;

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;

// X_VIDEO_MODE
void FillVideoMode(uint32_t mode) {
  std::memset(Ptr(mode), 0, 0x30);
  Store<uint32_t>(mode + 0x00, kWidth);
  Store<uint32_t>(mode + 0x04, kHeight);
  Store<uint32_t>(mode + 0x08, 0);  // interlaced
  Store<uint32_t>(mode + 0x0C, 1);  // widescreen
  Store<uint32_t>(mode + 0x10, 1);  // HD
  float refresh = 60.0f;
  uint32_t bits;
  std::memcpy(&bits, &refresh, 4);
  Store<uint32_t>(mode + 0x14, bits);
  Store<uint32_t>(mode + 0x18, 1);  // video standard NTSC-M
}

void VdQueryVideoMode(uint32_t mode) { FillVideoMode(mode); }

uint32_t VdQueryVideoFlags() { return 1 | 2; }  // widescreen, >= 1024 wide

void VdGetCurrentDisplayInformation(uint32_t info) {
  // X_DISPLAY_INFO (0x58 bytes); offsets from Xenia.
  std::memset(Ptr(info), 0, 0x58);
  Store<uint16_t>(info + 0x00, kWidth);   // front buffer width
  Store<uint16_t>(info + 0x02, kHeight);  // front buffer height
  // scaler parameters: source rect (x1, y1, x2, y2), scaled output size, filter types
  Store<uint32_t>(info + 0x0C, kWidth);
  Store<uint32_t>(info + 0x10, kHeight);
  Store<uint32_t>(info + 0x14, kWidth);
  Store<uint32_t>(info + 0x18, kHeight);
  Store<uint32_t>(info + 0x1C, 1);
  Store<uint32_t>(info + 0x20, 1);
  Store<uint16_t>(info + 0x40, 320);  // overscan left/top/right/bottom
  Store<uint16_t>(info + 0x42, 180);
  Store<uint16_t>(info + 0x44, 320);
  Store<uint16_t>(info + 0x46, 180);
  Store<uint16_t>(info + 0x48, kWidth);   // display width
  Store<uint16_t>(info + 0x4A, kHeight);  // display height
  float refresh = 60.0f;
  uint32_t bits;
  std::memcpy(&bits, &refresh, 4);
  Store<uint32_t>(info + 0x4C, bits);
  Store<uint32_t>(info + 0x50, 0);
  Store<uint16_t>(info + 0x54, kWidth);  // actual display width
}

void VdGetCurrentDisplayGamma(uint32_t type_ptr, uint32_t power_ptr) {
  Store<uint32_t>(type_ptr, 2);  // BT.709
  float power = 2.22222233f;
  uint32_t bits;
  std::memcpy(&bits, &power, 4);
  Store<uint32_t>(power_ptr, bits);
}

uint32_t VdSetDisplayMode(uint32_t flags) {
  (void)flags;
  return 0;
}
uint32_t VdSetDisplayModeOverride() { return 0; }

uint32_t VdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t arg, uint32_t pfp, uint32_t me) {
  (void)unk0, (void)callback, (void)arg, (void)pfp, (void)me;
  return 1;  // the GPU is set up before the title starts
}
void VdShutdownEngines() {}

uint32_t VdEnableDisableClockGating(uint32_t enabled) {
  (void)enabled;
  return 0;
}

void VdSetGraphicsInterruptCallback(uint32_t callback, uint32_t user_data) {
  gpu::SetInterruptCallback(callback, user_data);
}

void VdInitializeRingBuffer(uint32_t ptr, uint32_t size_log2) { gpu::InitializeRingBuffer(ptr, size_log2); }

void VdEnableRingBufferRPtrWriteBack(uint32_t ptr, uint32_t block_size_log2) {
  gpu::EnableReadPointerWriteBack(ptr, block_size_log2);
}

void VdGetSystemCommandBuffer(uint32_t p0, uint32_t p1) {
  std::memset(Ptr(p0), 0, 0x94);
  Store<uint32_t>(p0, 0xBEEF0000);
  Store<uint32_t>(p1, 0xBEEF0001);
}

void VdSetSystemCommandBufferGpuIdentifierAddress(uint32_t address) { (void)address; }

uint32_t VdInitializeScalerCommandBuffer(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                         uint32_t g, uint32_t h, uint32_t i, uint32_t j, uint32_t dest,
                                         uint32_t dest_count) {
  (void)a, (void)b, (void)c, (void)d, (void)e, (void)f, (void)g, (void)h, (void)i, (void)j;
  for (uint32_t n = 0; n < dest_count; ++n) Store<uint32_t>(dest + n * 4, 0x80000000);  // type-2 NOPs
  return dest_count;
}

uint32_t VdCallGraphicsNotificationRoutines(uint32_t unk0, uint32_t args) {
  (void)unk0, (void)args;
  return 0;
}

uint32_t VdIsHSIOTrainingSucceeded() { return 1; }

uint32_t VdPersistDisplay(uint32_t unk0, uint32_t out_ptr) {
  (void)unk0;
  if (out_ptr) Store<uint32_t>(out_ptr, PhysicalAlloc(64, 0x1000, 32, 0, 0));
  return 1;
}

uint32_t VdRetrainEDRAMWorker(uint32_t unk0) {
  (void)unk0;
  return 0;
}
uint32_t VdRetrainEDRAM() { return 0; }

// VdSwap writes into 64 dwords the title reserved in the ring buffer: the front buffer's texture
// fetch constant and a PM4_XE_SWAP packet the command processor turns into a present.
}  // namespace
namespace x360::gpu {
extern std::atomic<uint64_t> g_swap_count;
}
namespace {

void VdSwap(uint32_t buffer, uint32_t fetch, uint32_t unk2, uint32_t unk3, uint32_t unk4, uint32_t frontbuffer_ptr,
            uint32_t format_ptr, uint32_t color_space_ptr, uint32_t width_ptr, uint32_t height_ptr) {
  (void)unk2, (void)unk3, (void)unk4, (void)format_ptr, (void)color_space_ptr;
  ++x360::gpu::g_swap_count;
  x360::gpu::NotifySwap();
  uint32_t fetch_dwords[6];
  for (int i = 0; i < 6; ++i) fetch_dwords[i] = Load<uint32_t>(fetch + i * 4);
  uint32_t frontbuffer_virtual = fetch_dwords[1] & 0xFFFFF000;
  uint32_t frontbuffer_physical = GuestToPhysical(frontbuffer_virtual);
  fetch_dwords[1] = (fetch_dwords[1] & 0xFFF) | (frontbuffer_physical & 0xFFFFF000);
  (void)frontbuffer_ptr;

  // Width and height are pointers in some XDK versions and plain values in others (this title
  // passes 1280/720 directly); small values cannot be valid guest pointers.
  auto dimension = [](uint32_t arg, uint32_t fallback) {
    if (!arg) return fallback;
    return arg < 0x10000 ? arg : Load<uint32_t>(arg);
  };
  uint32_t width = dimension(width_ptr, kWidth);
  uint32_t height = dimension(height_ptr, kHeight);

  std::memset(Ptr(buffer), 0, 64 * 4);
  uint32_t o = 0;
  auto put = [&](uint32_t v) { Store<uint32_t>(buffer + 4 * o++, v); };
  put((5u << 16) | 0x4800);  // type-0: 6 registers at SHADER_CONSTANT_FETCH_00_0
  for (uint32_t v : fetch_dwords) put(v);
  put((3u << 30) | (3u << 16) | (0x64u << 8));  // type-3 XE_SWAP, 4 dwords
  put('SWAP');
  put(frontbuffer_physical);
  put(width);
  put(height);
  while (o < 64) put(0x80000000);  // type-2 NOPs
}

}  // namespace

namespace x360::kernel {
void InitVideo() {}
}  // namespace x360::kernel

X360_EXPORT(VdQueryVideoMode, VdQueryVideoMode)
X360_EXPORT(VdQueryVideoFlags, VdQueryVideoFlags)
X360_EXPORT(VdGetCurrentDisplayInformation, VdGetCurrentDisplayInformation)
X360_EXPORT(VdGetCurrentDisplayGamma, VdGetCurrentDisplayGamma)
X360_EXPORT(VdSetDisplayMode, VdSetDisplayMode)
X360_EXPORT(VdSetDisplayModeOverride, VdSetDisplayModeOverride)
X360_EXPORT(VdInitializeEngines, VdInitializeEngines)
X360_EXPORT(VdShutdownEngines, VdShutdownEngines)
X360_EXPORT(VdEnableDisableClockGating, VdEnableDisableClockGating)
X360_EXPORT(VdSetGraphicsInterruptCallback, VdSetGraphicsInterruptCallback)
X360_EXPORT(VdInitializeRingBuffer, VdInitializeRingBuffer)
X360_EXPORT(VdEnableRingBufferRPtrWriteBack, VdEnableRingBufferRPtrWriteBack)
X360_EXPORT(VdGetSystemCommandBuffer, VdGetSystemCommandBuffer)
X360_EXPORT(VdSetSystemCommandBufferGpuIdentifierAddress, VdSetSystemCommandBufferGpuIdentifierAddress)
X360_EXPORT(VdInitializeScalerCommandBuffer, VdInitializeScalerCommandBuffer)
X360_EXPORT(VdCallGraphicsNotificationRoutines, VdCallGraphicsNotificationRoutines)
X360_EXPORT(VdIsHSIOTrainingSucceeded, VdIsHSIOTrainingSucceeded)
X360_EXPORT(VdPersistDisplay, VdPersistDisplay)
X360_EXPORT(VdRetrainEDRAMWorker, VdRetrainEDRAMWorker)
X360_EXPORT(VdRetrainEDRAM, VdRetrainEDRAM)
X360_EXPORT(VdSwap, VdSwap)
