// PM4 command processor. Semantics follow Xenia's command processor (BSD licensed), reduced to
// what a title needs to make progress: register writes, fences, waits, interrupts and swaps.
#include <chrono>
#include <deque>
#include <thread>

#include "x360/gpu.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::gpu {

using namespace x360::kernel;

namespace {

enum Opcode : uint32_t {
  PM4_NOP = 0x10,
  PM4_REG_RMW = 0x21,
  PM4_DRAW_INDX = 0x22,
  PM4_VIZ_QUERY = 0x23,
  PM4_SET_STATE = 0x25,
  PM4_WAIT_FOR_IDLE = 0x26,
  PM4_IM_LOAD = 0x27,
  PM4_IM_LOAD_IMMEDIATE = 0x2B,
  PM4_SET_CONSTANT = 0x2D,
  PM4_LOAD_CONSTANT_CONTEXT = 0x2E,
  PM4_LOAD_ALU_CONSTANT = 0x2F,
  PM4_DRAW_INDX_BIN = 0x34,
  PM4_DRAW_INDX_2_BIN = 0x35,
  PM4_DRAW_INDX_2 = 0x36,
  PM4_INDIRECT_BUFFER_PFD = 0x37,
  PM4_INVALIDATE_STATE = 0x3B,
  PM4_WAIT_REG_MEM = 0x3C,
  PM4_MEM_WRITE = 0x3D,
  PM4_REG_TO_MEM = 0x3E,
  PM4_INDIRECT_BUFFER = 0x3F,
  PM4_COND_EXEC = 0x44,
  PM4_COND_WRITE = 0x45,
  PM4_EVENT_WRITE = 0x46,
  PM4_ME_INIT = 0x48,
  PM4_SET_SHADER_BASES = 0x4A,
  PM4_SET_BIN_BASE_OFFSET = 0x4B,
  PM4_MEM_WRITE_CNTR = 0x4F,
  PM4_SET_BIN_MASK = 0x50,
  PM4_SET_BIN_SELECT = 0x51,
  PM4_WAIT_REG_EQ = 0x52,
  PM4_WAIT_REG_GTE = 0x53,
  PM4_INTERRUPT = 0x54,
  PM4_SET_CONSTANT2 = 0x55,
  PM4_SET_SHADER_CONSTANTS = 0x56,
  PM4_EVENT_WRITE_SHD = 0x58,
  PM4_EVENT_WRITE_CFL = 0x59,
  PM4_EVENT_WRITE_EXT = 0x5A,
  PM4_EVENT_WRITE_ZPD = 0x5B,
  PM4_WAIT_UNTIL_READ = 0x5C,
  PM4_WAIT_IB_PFD_COMPLETE = 0x5D,
  PM4_CONTEXT_UPDATE = 0x5E,
  PM4_SET_BIN_MASK_LO = 0x60,
  PM4_SET_BIN_MASK_HI = 0x61,
  PM4_SET_BIN_SELECT_LO = 0x62,
  PM4_SET_BIN_SELECT_HI = 0x63,
  PM4_XE_SWAP = 0x64,
};

Renderer* g_renderer = nullptr;
std::atomic<uint32_t> g_ring_base{0};  // physical
uint32_t g_ring_dwords = 0;
std::atomic<uint32_t> g_writeback{0};  // physical
uint32_t g_read_ptr = 0;
uint64_t g_bin_mask = ~0ull, g_bin_select = ~0ull;
uint32_t g_counter = 0;  // swaps (also written by EVENT_WRITE_SHD counter events)
uint64_t g_zpd_samples = 0;

std::mutex g_stats_mutex;
Stats g_stats;

std::atomic<uint32_t> g_interrupt_callback{0};
std::atomic<uint32_t> g_interrupt_data{0};
std::mutex g_interrupt_mutex;
std::condition_variable g_interrupt_cv;
std::deque<uint32_t> g_interrupt_queue;  // sources

uint8_t* RegPtr(uint32_t index) { return g_base + kRegisterBase + index * 4; }

// Guest pointer for a physical address (any window works, they alias).
uint8_t* Phys(uint32_t physical) { return g_base + 0xA0000000u + (physical & 0x1FFFFFFF); }

uint32_t GpuSwap(uint32_t value, uint32_t endian) {
  switch (endian & 3) {
    case 1: return ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0x00FF00FF);  // 8in16
    case 2: return _byteswap_ulong(value);                                     // 8in32
    case 3: return (value << 16) | (value >> 16);                              // 16in32
    default: return value;
  }
}

// Writes `value` to a physical address the way the GPU does (endian swap, then little-endian store).
void GpuStore(uint32_t address_with_endian, uint32_t value) {
  uint32_t address = address_with_endian & ~3u;
  uint32_t data = GpuSwap(value, address_with_endian & 3);
  uint8_t* destination = Phys(address);
  if (address > 0x1FFFFFFF) {
    uint32_t wb_base = ReadRegister(reg::WRITEBACK_START);
    uint32_t wb_size = ReadRegister(reg::WRITEBACK_SIZE);
    uint32_t offset = address - wb_base;
    if (wb_base && offset < wb_size) destination = g_base + 0x7F000000 + offset;
  }
  std::memcpy(destination, &data, 4);
}

uint32_t GpuLoad(uint32_t address_with_endian) {
  uint32_t data;
  std::memcpy(&data, Phys(address_with_endian & ~3u), 4);
  return GpuSwap(data, address_with_endian & 3);
}

void QueueInterrupt(uint32_t source) {
  std::lock_guard lock(g_interrupt_mutex);
  g_interrupt_queue.push_back(source);
  g_interrupt_cv.notify_all();
}

// Reads dwords from either the ring (with wraparound) or an indirect buffer.
struct Reader {
  uint32_t base;      // physical
  uint32_t size;      // dwords (ring size, or buffer length)
  uint32_t pos;       // dword index
  uint32_t end;       // dword index to stop at
  bool ring;

  uint32_t Remaining() const { return ring ? (end + size - pos) % size : end - pos; }
  uint32_t Read() {
    uint32_t v = load_be<uint32_t>(Phys(base + pos * 4));
    pos = ring ? (pos + 1) % size : pos + 1;
    return v;
  }
  void Skip(uint32_t n) { pos = ring ? (pos + n) % size : pos + n; }
  const uint32_t* Here() const { return reinterpret_cast<const uint32_t*>(Phys(base + pos * 4)); }
};

bool ExecutePacket(Reader& r);

void ExecuteIndirect(uint32_t physical, uint32_t dwords) {
  Reader r{physical & 0x1FFFFFFF, dwords, 0, dwords, false};
  while (r.Remaining() > 0) {
    if (!ExecutePacket(r)) break;
  }
}

bool MatchRef(uint32_t value, uint32_t ref, uint32_t wait_info) {
  switch (wait_info & 7) {
    case 0: return false;
    case 1: return value < ref;
    case 2: return value <= ref;
    case 3: return value == ref;
    case 4: return value != ref;
    case 5: return value >= ref;
    case 6: return value > ref;
    default: return true;
  }
}

void WriteRegisterRange(Reader& r, uint32_t base, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) WriteRegister(base + i, r.Read());
}

void WriteRegisterRangeFromMemory(uint32_t base, uint32_t physical, uint32_t count) {
  const uint8_t* src = Phys(physical);
  for (uint32_t i = 0; i < count; ++i) WriteRegister(base + i, load_be<uint32_t>(src + i * 4));
}

uint32_t ConstantBase(uint32_t type) {
  switch (type) {
    case 0: return 0x4000;  // ALU
    case 1: return 0x4800;  // fetch
    case 2: return 0x4900;  // bool
    case 3: return 0x4908;  // loop
    case 4: return 0x2000;  // registers
    default: return 0xFFFFFFFF;
  }
}

void IssueDraw(Reader& r, uint32_t count) {
  uint32_t initiator = r.Read();
  --count;
  WriteRegister(reg::VGT_DRAW_INITIATOR, initiator);
  DrawInfo draw{};
  draw.prim_type = initiator & 0x3F;
  uint32_t source = (initiator >> 6) & 3;
  draw.index_size = ((initiator >> 11) & 1) ? 4 : 2;
  draw.index_count = initiator >> 16;
  if (source == 0 && count >= 2) {  // DMA: indexed
    draw.indexed = true;
    draw.index_base = r.Read() & 0x1FFFFFFF;
    uint32_t dma_size = r.Read();
    count -= 2;
    draw.index_endian = dma_size >> 30;
  }
  r.Skip(count);
  {
    std::lock_guard lock(g_stats_mutex);
    ++g_stats.draws;
  }
  if (g_renderer) g_renderer->Draw(draw);
}

bool ExecuteType3(Reader& r, uint32_t packet) {
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  if (r.Remaining() < count) {
    KLOG_W("PM4: packet %02X overflows the buffer", opcode);
    return false;
  }
  if ((packet & 1) && !(g_bin_select & g_bin_mask)) {  // predicated, all bins masked off
    r.Skip(count);
    return true;
  }

  switch (opcode) {
    case PM4_ME_INIT:
    case PM4_NOP:
    case PM4_INVALIDATE_STATE:
    case PM4_WAIT_IB_PFD_COMPLETE:
    case PM4_WAIT_FOR_IDLE:
    case PM4_CONTEXT_UPDATE:
    case PM4_SET_SHADER_BASES:
    case PM4_SET_BIN_BASE_OFFSET:
    case PM4_SET_STATE:
    case PM4_LOAD_CONSTANT_CONTEXT:
    case PM4_WAIT_UNTIL_READ:
    case PM4_EVENT_WRITE_CFL:
    case PM4_VIZ_QUERY:
      r.Skip(count);
      return true;

    case PM4_INTERRUPT: {
      uint32_t cpu_mask = r.Read();
      r.Skip(count - 1);
      (void)cpu_mask;
      QueueInterrupt(1);
      return true;
    }

    case PM4_XE_SWAP: {
      r.Read();  // signature 'SWAP'
      uint32_t frontbuffer = r.Read();
      uint32_t width = r.Read();
      uint32_t height = r.Read();
      r.Skip(count - 4);
      ++g_counter;
      {
        std::lock_guard lock(g_stats_mutex);
        ++g_stats.swaps;
      }
      if (g_renderer) g_renderer->Swap(frontbuffer, width, height);
      return true;
    }

    case PM4_INDIRECT_BUFFER:
    case PM4_INDIRECT_BUFFER_PFD: {
      uint32_t address = r.Read();
      uint32_t length = r.Read() & 0xFFFFF;
      r.Skip(count - 2);
      ExecuteIndirect(address, length);
      return true;
    }

    case PM4_WAIT_REG_MEM:
    case PM4_WAIT_REG_EQ:
    case PM4_WAIT_REG_GTE: {
      uint32_t wait_info = r.Read();
      uint32_t poll = r.Read();
      uint32_t ref = r.Read();
      uint32_t mask = r.Read();
      uint32_t wait = r.Read();
      r.Skip(count - 5);
      if (opcode == PM4_WAIT_REG_EQ) wait_info = 3;
      if (opcode == PM4_WAIT_REG_GTE) wait_info = 5;
      bool memory = (wait_info & 0x10) != 0;
      auto start = std::chrono::steady_clock::now();
      for (;;) {
        uint32_t value;
        if (memory) {
          value = GpuLoad(poll);
        } else {
          // Cache coherency requests complete immediately (memory is shared): clear the busy bit.
          // (Direct store: WriteRegister sets the bit, as a CPU-side write of this register does.)
          if (poll == reg::COHER_STATUS_HOST) store_be<uint32_t>(RegPtr(poll), ReadRegister(poll) & 0x7FFFFFFF);
          value = ReadRegister(poll);
        }
        if (MatchRef(value & mask, ref, wait_info)) break;
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
          KLOG_W("PM4 WAIT_REG_MEM timed out (%s %08X, ref %08X, mask %08X, fn %u)", memory ? "mem" : "reg", poll, ref,
                 mask, wait_info & 7);
          break;
        }
        if (wait >= 0x100) std::this_thread::sleep_for(std::chrono::milliseconds(wait / 0x100));
        else std::this_thread::yield();
      }
      return true;
    }

    case PM4_REG_RMW: {
      uint32_t info = r.Read(), and_mask = r.Read(), or_mask = r.Read();
      r.Skip(count - 3);
      uint32_t value = ReadRegister(info & 0x1FFF);
      value &= (info >> 31) & 1 ? ReadRegister(and_mask & 0x1FFF) : and_mask;
      value |= (info >> 30) & 1 ? ReadRegister(or_mask & 0x1FFF) : or_mask;
      WriteRegister(info & 0x1FFF, value);
      return true;
    }

    case PM4_REG_TO_MEM: {
      uint32_t reg_index = r.Read(), address = r.Read();
      r.Skip(count - 2);
      GpuStore(address, ReadRegister(reg_index));
      return true;
    }

    case PM4_MEM_WRITE: {
      uint32_t address = r.Read();
      for (uint32_t i = 1; i < count; ++i, address += 4) GpuStore(address, r.Read());
      return true;
    }

    case PM4_COND_WRITE: {
      uint32_t wait_info = r.Read(), poll = r.Read(), ref = r.Read(), mask = r.Read();
      uint32_t write_address = r.Read(), data = r.Read();
      r.Skip(count - 6);
      uint32_t value = (wait_info & 0x10) ? GpuLoad(poll) : ReadRegister(poll);
      if (MatchRef(value & mask, ref, wait_info)) {
        if (wait_info & 0x100) GpuStore(write_address, data);
        else WriteRegister(write_address, data);
      }
      return true;
    }

    case PM4_EVENT_WRITE: {
      uint32_t initiator = r.Read();
      r.Skip(count - 1);
      WriteRegister(reg::VGT_EVENT_INITIATOR, initiator & 0x3F);
      return true;
    }

    case PM4_EVENT_WRITE_SHD: {
      uint32_t initiator = r.Read(), address = r.Read(), value = r.Read();
      r.Skip(count - 3);
      WriteRegister(reg::VGT_EVENT_INITIATOR, initiator & 0x3F);
      GpuStore(address, (initiator >> 31) & 1 ? g_counter : value);
      return true;
    }

    case PM4_EVENT_WRITE_EXT: {
      uint32_t initiator = r.Read(), address = r.Read();
      r.Skip(count - 2);
      WriteRegister(reg::VGT_EVENT_INITIATOR, initiator & 0x3F);
      // Screen extents of earlier draws: report "everything".
      const uint16_t extents[6] = {0, 8192 >> 3, 0, 8192 >> 3, 0, 1};
      uint8_t* dst = Phys(address & ~3u);
      for (int i = 0; i < 6; ++i) store_be<uint16_t>(dst + i * 2, extents[i]);
      return true;
    }

    case PM4_EVENT_WRITE_ZPD: {
      uint32_t initiator = r.Read();
      r.Skip(count - 1);
      WriteRegister(reg::VGT_EVENT_INITIATOR, initiator & 0x3F);
      if (uint32_t report = ReadRegister(reg::RB_SAMPLE_COUNT_ADDR)) {
        // Occlusion queries: a free-running counter that always grows, i.e. "visible".
        g_zpd_samples += 0x10000;
        uint32_t half = uint32_t(g_zpd_samples / 2);
        uint32_t counts[8] = {half, half, 0, 0, half, half, 0, 0};  // Total, ZFail, ZPass, StencilFail (A/B)
        std::memcpy(Phys(report), counts, sizeof(counts));          // little-endian, as Xenos writes it
      }
      return true;
    }

    case PM4_DRAW_INDX:
      r.Read();  // viz query token
      IssueDraw(r, count - 1);
      return true;
    case PM4_DRAW_INDX_2:
      IssueDraw(r, count);
      return true;

    case PM4_SET_CONSTANT: {
      uint32_t offset_type = r.Read();
      uint32_t base = ConstantBase((offset_type >> 16) & 0xFF);
      if (base == 0xFFFFFFFF) r.Skip(count - 1);
      else WriteRegisterRange(r, base + (offset_type & 0x7FF), count - 1);
      return true;
    }
    case PM4_SET_CONSTANT2:
    case PM4_SET_SHADER_CONSTANTS: {
      uint32_t offset_type = r.Read();
      WriteRegisterRange(r, offset_type & 0xFFFF, count - 1);
      return true;
    }
    case PM4_LOAD_ALU_CONSTANT: {
      uint32_t address = r.Read() & 0x3FFFFFFF;
      uint32_t offset_type = r.Read();
      uint32_t size = r.Read() & 0xFFF;
      r.Skip(count - 3);
      uint32_t base = ConstantBase((offset_type >> 16) & 0xFF);
      if (base != 0xFFFFFFFF) WriteRegisterRangeFromMemory(base + (offset_type & 0x7FF), address, size);
      return true;
    }

    case PM4_IM_LOAD: {
      uint32_t addr_type = r.Read();
      uint32_t start_size = r.Read();
      r.Skip(count - 2);
      if (g_renderer) {
        uint32_t address = addr_type & ~3u;
        g_renderer->LoadShader({addr_type & 3, reinterpret_cast<const uint32_t*>(Phys(address)), start_size & 0xFFFF,
                                address});
      }
      return true;
    }
    case PM4_IM_LOAD_IMMEDIATE: {
      uint32_t type = r.Read();
      uint32_t start_size = r.Read();
      uint32_t size = start_size & 0xFFFF;
      if (g_renderer) g_renderer->LoadShader({type, r.Here(), size, 0});
      r.Skip(count - 2);
      return true;
    }

    case PM4_SET_BIN_MASK_LO: g_bin_mask = (g_bin_mask & ~0xFFFFFFFFull) | r.Read(); r.Skip(count - 1); return true;
    case PM4_SET_BIN_MASK_HI: g_bin_mask = (g_bin_mask & 0xFFFFFFFFull) | (uint64_t(r.Read()) << 32); r.Skip(count - 1); return true;
    case PM4_SET_BIN_SELECT_LO: g_bin_select = (g_bin_select & ~0xFFFFFFFFull) | r.Read(); r.Skip(count - 1); return true;
    case PM4_SET_BIN_SELECT_HI: g_bin_select = (g_bin_select & 0xFFFFFFFFull) | (uint64_t(r.Read()) << 32); r.Skip(count - 1); return true;
    case PM4_SET_BIN_MASK: {
      uint64_t hi = r.Read(), lo = r.Read();
      g_bin_mask = (hi << 32) | lo;
      r.Skip(count - 2);
      return true;
    }
    case PM4_SET_BIN_SELECT: {
      uint64_t hi = r.Read(), lo = r.Read();
      g_bin_select = (hi << 32) | lo;
      r.Skip(count - 2);
      return true;
    }

    default: {
      static std::atomic<int> warned{0};
      if (warned++ < 32) KLOG_W("PM4: unimplemented opcode %02X (count %u)", opcode, count);
      r.Skip(count);
      return true;
    }
  }
}

bool ExecutePacket(Reader& r) {
  uint32_t packet = r.Read();
  {
    std::lock_guard lock(g_stats_mutex);
    ++g_stats.packets;
  }
  if (packet == 0 || packet == 0x0BADF00D) return true;
  switch (packet >> 30) {
    case 0: {  // register writes
      uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      if (r.Remaining() < count) return false;
      uint32_t base = packet & 0x7FFF;
      bool one_reg = (packet >> 15) & 1;
      for (uint32_t i = 0; i < count; ++i) WriteRegister(one_reg ? base : base + i, r.Read());
      return true;
    }
    case 1: {  // two register writes
      uint32_t a = r.Read(), b = r.Read();
      WriteRegister(packet & 0x7FF, a);
      WriteRegister((packet >> 11) & 0x7FF, b);
      return true;
    }
    case 2:  // NOP
      return true;
    default:
      return ExecuteType3(r, packet);
  }
}

void CommandProcessorThread() {
  SetThreadDescription(GetCurrentThread(), L"GPU command processor");
  unsigned idle = 0;
  for (;;) {
    uint32_t ring = g_ring_base.load();
    uint32_t write_ptr = ReadRegister(reg::CP_RB_WPTR);
    if (!ring || !g_ring_dwords || write_ptr == g_read_ptr || write_ptr >= g_ring_dwords) {
      if (++idle > 2000) std::this_thread::sleep_for(std::chrono::microseconds(500));
      else std::this_thread::yield();
      continue;
    }
    idle = 0;
    Reader r{ring, g_ring_dwords, g_read_ptr, write_ptr, true};
    while (r.pos != write_ptr) {
      if (!ExecutePacket(r)) {
        r.pos = write_ptr;
        break;
      }
    }
    g_read_ptr = write_ptr;
    WriteRegister(reg::CP_RB_RPTR, g_read_ptr);
    if (uint32_t wb = g_writeback.load()) store_be<uint32_t>(Phys(wb), g_read_ptr);
  }
}

// Delivers interrupts (vblank every 16.7 ms, plus PM4_INTERRUPT requests) to the title's callback
// on a guest thread, as the kernel's DPC would.
void InterruptThreadBody() {
  using clock = std::chrono::steady_clock;
  auto next_vblank = clock::now();
  for (;;) {
    uint32_t source = 0;
    bool vblank = false;
    {
      std::unique_lock lock(g_interrupt_mutex);
      g_interrupt_cv.wait_until(lock, next_vblank, [] { return !g_interrupt_queue.empty(); });
      if (!g_interrupt_queue.empty()) {
        source = g_interrupt_queue.front();
        g_interrupt_queue.pop_front();
      } else {
        vblank = true;
      }
    }
    if (vblank) {
      next_vblank += std::chrono::microseconds(16667);
      if (next_vblank < clock::now()) next_vblank = clock::now() + std::chrono::microseconds(16667);
      std::lock_guard lock(g_stats_mutex);
      ++g_stats.vblanks;
    }
    uint32_t callback = g_interrupt_callback.load();
    if (!callback) continue;
    WriteRegister(reg::D1MODE_INTERRUPT_STATUS, 1);
    Thread* t = CurrentThread();
    Store<uint8_t>(t->pcr + 0x18, 2);       // IRQL: DISPATCH_LEVEL
    Store<uint32_t>(t->pcr + 0x150, 1);     // PRCB dpc_active
    CallGuest(callback, {vblank ? 0u : source, g_interrupt_data.load()});
    Store<uint32_t>(t->pcr + 0x150, 0);
    Store<uint8_t>(t->pcr + 0x18, 0);
  }
}

}  // namespace

uint32_t ReadRegister(uint32_t index) { return load_be<uint32_t>(RegPtr(index)); }

void WriteRegister(uint32_t index, uint32_t value) {
  if (index >= kRegisterCount) return;
  if (index == reg::COHER_STATUS_HOST) value |= 0x80000000u;
  store_be<uint32_t>(RegPtr(index), value);
  if (index >= reg::SCRATCH_REG0 && index < reg::SCRATCH_REG0 + 8) {
    uint32_t n = index - reg::SCRATCH_REG0;
    if (ReadRegister(reg::SCRATCH_UMSK) & (1u << n)) {
      store_be<uint32_t>(Phys(ReadRegister(reg::SCRATCH_ADDR) + n * 4), value);
    }
  }
}

void SetRenderer(Renderer* renderer) { g_renderer = renderer; }

void Initialize() {
  static std::once_flag once;
  std::call_once(once, [] {
    // Registers the title reads back during initialization (values from real hardware).
    store_be<uint32_t>(RegPtr(reg::RB_EDRAM_TIMING), 0x08100748);
    store_be<uint32_t>(RegPtr(reg::RB_BC_CONTROL), 0x0000200E);
    store_be<uint32_t>(RegPtr(reg::RB_HSIO_INTERFACE_ALIGNER_VALUE), 0x00BBBBBB);
    store_be<uint32_t>(RegPtr(reg::D1MODE_INTERRUPT_STATUS), 1);
    store_be<uint32_t>(RegPtr(reg::D1MODE_VIEWPORT_SIZE), 0x050002D0);
    std::thread(CommandProcessorThread).detach();
    std::thread([] {
      SetThreadDescription(GetCurrentThread(), L"GPU stats");
      Stats last{};
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        Stats s = GetStats();
        KLOG_I("GPU: %llu packets, %llu draws, %llu swaps (+%llu), %llu vblanks, rptr %u/wptr %u",
               (unsigned long long)s.packets, (unsigned long long)s.draws, (unsigned long long)s.swaps,
               (unsigned long long)(s.swaps - last.swaps), (unsigned long long)s.vblanks, g_read_ptr,
               ReadRegister(reg::CP_RB_WPTR));
        last = s;
      }
    }).detach();
    CreateHostDrivenThread("GPU interrupts", 2, InterruptThreadBody);
    KLOG_I("GPU: command processor started");
  });
}

void InitializeRingBuffer(uint32_t physical, uint32_t size_log2) {
  physical &= 0x1FFFFFFF;
  uint32_t bytes = 1u << (size_log2 + 3);
  std::memset(Phys(physical), 0, bytes);
  g_read_ptr = 0;
  g_ring_dwords = bytes / 4;
  g_ring_base = physical;
  KLOG_I("GPU: ring buffer at %08X, %u bytes", physical, bytes);
}

void EnableReadPointerWriteBack(uint32_t physical, uint32_t block_size_log2) {
  (void)block_size_log2;
  g_writeback = physical & 0x1FFFFFFF;
  KLOG_I("GPU: read pointer write-back at %08X", physical);
}

void SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  g_interrupt_data = user_data;
  g_interrupt_callback = callback;
  KLOG_I("GPU: interrupt callback %08X (%08X)", callback, user_data);
}

Stats GetStats() {
  std::lock_guard lock(g_stats_mutex);
  return g_stats;
}

}  // namespace x360::gpu
