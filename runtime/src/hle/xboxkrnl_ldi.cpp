// LDI, the kernel's LZX decompressor (the cabinet "LZX Decompression Interface"). In the 2010 XDKs
// XAPI's XMemDecompress calls it for each block of a compressed stream: one LDIDecompress per block,
// each block one LZX frame of up to 32 KiB, the decoder's window and trees carried from one block to
// the next until LDIResetDecompression. Decoded with libmspack's lzxd, as the XEX images are.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "x360/kernel.h"

extern "C" {
#include <mspack.h>
#include <lzx.h>
}

using namespace x360::kernel;

namespace {

// MDI_ERROR_*.
constexpr uint32_t kMdiOk = 0;
constexpr uint32_t kMdiNotEnoughMemory = 1;
constexpr uint32_t kMdiBadParameters = 2;
constexpr uint32_t kMdiBufferOverflow = 3;
constexpr uint32_t kMdiFailed = 4;
constexpr uint32_t kMdiConfiguration = 5;

constexpr uint32_t kFrameSize = 32768;
constexpr uint32_t kMaxGrowth = 6144;  // compressed data can exceed its block by this much

// An mspack_file over a buffer; the struct is what lzxd gets as its "file".
struct MemFile {
  uint8_t* data = nullptr;
  size_t size = 0;
  size_t pos = 0;
};

int MemRead(mspack_file* f, void* buffer, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  if (bytes < 0) return -1;
  const size_t n = std::min<size_t>(size_t(bytes), m->size - m->pos);
  std::memcpy(buffer, m->data + m->pos, n);
  m->pos += n;
  return int(n);
}
int MemWrite(mspack_file* f, void* buffer, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  if (bytes < 0) return -1;
  const size_t n = std::min<size_t>(size_t(bytes), m->size - m->pos);
  std::memcpy(m->data + m->pos, buffer, n);
  m->pos += n;
  return int(n);  // short on overflow, which makes lzxd fail
}
void* MemAlloc(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
void MemFree(void* p) { std::free(p); }
void MemCopy(void* src, void* dst, size_t bytes) { std::memmove(dst, src, bytes); }
void MemMessage(mspack_file*, const char*, ...) {}

struct Context {
  mspack_system sys{};
  MemFile in, out;
  int window_bits = 0;
  lzxd_stream* lzx = nullptr;

  ~Context() {
    if (lzx) lzxd_free(lzx);
  }
  bool Reset() {
    if (lzx) lzxd_free(lzx);
    lzx = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&in), reinterpret_cast<mspack_file*>(&out), window_bits, 0,
                    4096, 0, 0);
    return lzx != nullptr;
  }
};

std::mutex g_mutex;
std::unordered_map<uint32_t, std::unique_ptr<Context>> g_contexts;
uint32_t g_next_handle = 0x4C440001;  // 'LD..'

Context* Find(uint32_t handle) {
  auto it = g_contexts.find(handle);
  return it == g_contexts.end() ? nullptr : it->second.get();
}

// LDICreateDecompression(UINT* block_max, LZXDECOMPRESS* config {window size, CPU type},
// alloc, free, memory size, UINT* src_buffer_min, LDI_CONTEXT_HANDLE* handle, ...): one argument
// more than the cabinet version; XAPI and the avatar library pass a size (the window plus the
// decoder's state) before the two pointers. The memory callbacks and that size aren't needed.
uint32_t LDICreateDecompression(uint32_t block_max_ptr, uint32_t config, uint32_t alloc, uint32_t free_fn,
                                uint32_t memory_size, uint32_t src_buffer_min_ptr, uint32_t handle_ptr) {
  (void)alloc, (void)free_fn, (void)memory_size;
  if (!block_max_ptr || !config || !handle_ptr) return kMdiBadParameters;
  uint32_t block_max = Load<uint32_t>(block_max_ptr);
  if (block_max == 0 || block_max > kFrameSize) block_max = kFrameSize;
  const uint32_t window = Load<uint32_t>(config);
  int bits = 0;
  while (bits < 32 && (1u << bits) < window) ++bits;
  if ((1u << bits) != window || bits < 15 || bits > 21) {
    KLOG_W("LDICreateDecompression: window size %08X not supported", window);
    return kMdiConfiguration;
  }
  auto context = std::make_unique<Context>();
  context->sys.read = MemRead;
  context->sys.write = MemWrite;
  context->sys.message = MemMessage;
  context->sys.alloc = MemAlloc;
  context->sys.free = MemFree;
  context->sys.copy = MemCopy;
  context->window_bits = bits;
  if (!context->Reset()) return kMdiNotEnoughMemory;
  Store<uint32_t>(block_max_ptr, block_max);
  if (src_buffer_min_ptr) Store<uint32_t>(src_buffer_min_ptr, block_max + kMaxGrowth);
  std::lock_guard lock(g_mutex);
  const uint32_t handle = g_next_handle++;
  g_contexts[handle] = std::move(context);
  Store<uint32_t>(handle_ptr, handle);
  return kMdiOk;
}

// LDIDecompress(handle, source, source size, destination, UINT* result): *result is the block's
// uncompressed size on the way in (a full frame, or less for the stream's last block) and what was
// written on the way out.
uint32_t LDIDecompress(uint32_t handle, uint32_t source, uint32_t source_size, uint32_t destination,
                       uint32_t result_ptr) {
  std::lock_guard lock(g_mutex);
  Context* c = Find(handle);
  if (!c || !source || !destination || !result_ptr) return kMdiBadParameters;
  const uint32_t wanted = Load<uint32_t>(result_ptr);
  if (wanted == 0 || wanted > kFrameSize) return kMdiBufferOverflow;
  c->in = {Ptr<uint8_t>(source), source_size, 0};
  c->out = {Ptr<uint8_t>(destination), wanted, 0};
  // Each block starts on a fresh 16-bit word: what lzxd read ahead at the end of the last frame
  // (padding it made up past that block's end) isn't part of this one.
  lzxd_stream* lzx = c->lzx;
  lzx->i_ptr = lzx->i_end = lzx->inbuf;
  lzx->bit_buffer = 0;
  lzx->bits_left = 0;
  lzx->input_end = 0;
  lzx->error = MSPACK_ERR_OK;
  // The stream ends with this block as far as lzxd knows, so it doesn't decode the next frame ahead.
  lzxd_set_output_length(lzx, lzx->offset + wanted);
  const int error = lzxd_decompress(lzx, wanted);
  Store<uint32_t>(result_ptr, uint32_t(c->out.pos));
  if (error != MSPACK_ERR_OK) {
    KLOG_W("LDIDecompress: lzxd error %d (%u bytes in, %u wanted)", error, source_size, wanted);
    return kMdiFailed;
  }
  return kMdiOk;
}

uint32_t LDIResetDecompression(uint32_t handle) {
  std::lock_guard lock(g_mutex);
  Context* c = Find(handle);
  if (!c) return kMdiBadParameters;
  return c->Reset() ? kMdiOk : kMdiNotEnoughMemory;
}

uint32_t LDIDestroyDecompression(uint32_t handle) {
  std::lock_guard lock(g_mutex);
  return g_contexts.erase(handle) ? kMdiOk : kMdiBadParameters;
}

}  // namespace

X360_EXPORT(LDICreateDecompression, LDICreateDecompression)
X360_EXPORT(LDIDecompress, LDIDecompress)
X360_EXPORT(LDIResetDecompression, LDIResetDecompression)
X360_EXPORT(LDIDestroyDecompression, LDIDestroyDecompression)
