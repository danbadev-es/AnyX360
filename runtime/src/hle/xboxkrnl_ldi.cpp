// LDI, the kernel's LZX decompressor (the cabinet "LZX Decompression Interface"). In the 2010 XDKs
// XAPI's XMemDecompress and the avatar library call it for each block of a compressed stream: one
// LDIDecompress per block, each block one LZX frame of up to 32 KiB, the decoder's window and trees
// carried from one block to the next until LDIResetDecompression (x360tool's LzxFrameDecoder).
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "lzx_frames.h"
#include "x360/kernel.h"

using namespace x360::kernel;

namespace {

// MDI_ERROR_*.
constexpr uint32_t kMdiOk = 0;
constexpr uint32_t kMdiNotEnoughMemory = 1;
constexpr uint32_t kMdiBadParameters = 2;
constexpr uint32_t kMdiBufferOverflow = 3;
constexpr uint32_t kMdiFailed = 4;
constexpr uint32_t kMdiConfiguration = 5;

constexpr uint32_t kFrameSize = x360tool::LzxFrameDecoder::kFrameSize;
constexpr uint32_t kMaxGrowth = 6144;  // compressed data can exceed its block by this much

std::mutex g_mutex;
std::unordered_map<uint32_t, std::unique_ptr<x360tool::LzxFrameDecoder>> g_contexts;
uint32_t g_next_handle = 0x4C440001;  // 'LD..'

x360tool::LzxFrameDecoder* Find(uint32_t handle) {
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
  auto decoder = std::make_unique<x360tool::LzxFrameDecoder>(window);
  if (!decoder->ok()) {
    KLOG_W("LDICreateDecompression: window size %08X not supported", window);
    return kMdiConfiguration;
  }
  Store<uint32_t>(block_max_ptr, block_max);
  if (src_buffer_min_ptr) Store<uint32_t>(src_buffer_min_ptr, block_max + kMaxGrowth);
  std::lock_guard lock(g_mutex);
  const uint32_t handle = g_next_handle++;
  g_contexts[handle] = std::move(decoder);
  Store<uint32_t>(handle_ptr, handle);
  return kMdiOk;
}

// LDIDecompress(handle, source, source size, destination, UINT* result): *result is the block's
// uncompressed size on the way in (a full frame, or less for the stream's last block) and what was
// written on the way out.
uint32_t LDIDecompress(uint32_t handle, uint32_t source, uint32_t source_size, uint32_t destination,
                       uint32_t result_ptr) {
  std::lock_guard lock(g_mutex);
  x360tool::LzxFrameDecoder* decoder = Find(handle);
  if (!decoder || !source || !destination || !result_ptr) return kMdiBadParameters;
  const uint32_t wanted = Load<uint32_t>(result_ptr);
  if (wanted == 0 || wanted > kFrameSize) return kMdiBufferOverflow;
  std::string error;
  const bool ok = decoder->decode(std::span<const uint8_t>(Ptr<uint8_t>(source), source_size),
                                  std::span<uint8_t>(Ptr<uint8_t>(destination), wanted), error);
  Store<uint32_t>(result_ptr, ok ? wanted : 0);
  if (!ok) {
    KLOG_W("LDIDecompress: %s (%u bytes in, %u wanted)", error.c_str(), source_size, wanted);
    return kMdiFailed;
  }
  return kMdiOk;
}

uint32_t LDIResetDecompression(uint32_t handle) {
  std::lock_guard lock(g_mutex);
  x360tool::LzxFrameDecoder* decoder = Find(handle);
  if (!decoder) return kMdiBadParameters;
  return decoder->reset() ? kMdiOk : kMdiNotEnoughMemory;
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
