// Xenos GPU front end: the PM4 command processor that consumes what the title's statically linked
// Direct3D writes into the ring buffer, plus interrupts and vblank. Drawing is delegated to a
// Renderer; without one, draws are counted and dropped (the title still runs).
#pragma once

#include <cstdint>
#include <functional>

namespace x360::gpu {

// Register file: the title reads/writes GPU registers at 0x7FC80000 + index * 4 (big-endian),
// which is plain guest memory here; the command processor uses the same storage.
constexpr uint32_t kRegisterBase = 0x7FC80000;
constexpr uint32_t kRegisterCount = 0x5003;

namespace reg {
constexpr uint32_t CP_RB_RPTR = 0x01C4;
constexpr uint32_t CP_RB_WPTR = 0x01C5;
constexpr uint32_t SCRATCH_UMSK = 0x01DC;
constexpr uint32_t SCRATCH_ADDR = 0x01DD;
constexpr uint32_t SCRATCH_REG0 = 0x0578;
constexpr uint32_t WRITEBACK_START = 0x0A04;
constexpr uint32_t WRITEBACK_SIZE = 0x0A05;
constexpr uint32_t COHER_STATUS_HOST = 0x0A31;
constexpr uint32_t RB_EDRAM_TIMING = 0x0F00;
constexpr uint32_t RB_BC_CONTROL = 0x0F01;
constexpr uint32_t RB_HSIO_INTERFACE_ALIGNER_VALUE = 0x0F2D;
constexpr uint32_t D1MODE_INTERRUPT_STATUS = 0x1951;
constexpr uint32_t D1MODE_VIEWPORT_SIZE = 0x1961;
constexpr uint32_t VGT_DRAW_INITIATOR = 0x21FC;
constexpr uint32_t VGT_EVENT_INITIATOR = 0x21F9;
constexpr uint32_t VGT_DMA_BASE = 0x21FA;
constexpr uint32_t VGT_DMA_SIZE = 0x21FB;
constexpr uint32_t RB_SAMPLE_COUNT_ADDR = 0x2325;
constexpr uint32_t SHADER_CONSTANT_000_X = 0x4000;
constexpr uint32_t SHADER_CONSTANT_FETCH_00_0 = 0x4800;
}  // namespace reg

uint32_t ReadRegister(uint32_t index);
void WriteRegister(uint32_t index, uint32_t value);

struct DrawInfo {
  uint32_t prim_type;
  uint32_t index_count;
  bool indexed;
  uint32_t index_base;  // physical
  uint32_t index_size;  // 2 or 4 bytes
  uint32_t index_endian;
};

struct ShaderLoad {
  uint32_t type;  // 0 vertex, 1 pixel
  const uint32_t* dwords;  // big-endian microcode in guest memory
  uint32_t size_dwords;
  uint32_t guest_address;  // 0 for immediate loads
};

// Back end interface (D3D12 renderer, or none).
struct Renderer {
  virtual ~Renderer() = default;
  virtual void LoadShader(const ShaderLoad& shader) = 0;
  virtual void Draw(const DrawInfo& draw) = 0;
  virtual void Swap(uint32_t frontbuffer_physical, uint32_t width, uint32_t height) = 0;
};

void SetRenderer(Renderer* renderer);

// Called by the Vd* exports.
void Initialize();
void InitializeRingBuffer(uint32_t physical, uint32_t size_log2);
void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2);
void SetInterruptCallback(uint32_t callback, uint32_t user_data);

struct Stats {
  uint64_t packets = 0;
  uint64_t draws = 0;
  uint64_t swaps = 0;
  uint64_t vblanks = 0;
};
Stats GetStats();

}  // namespace x360::gpu
