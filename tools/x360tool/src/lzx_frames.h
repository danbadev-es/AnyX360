// LZX decoding one frame (up to 32 KiB of output) at a time, as the console's LDI does (the kernel's
// LDICreateDecompression/LDIDecompress, used by XMemDecompress and the avatar libraries): the window
// and trees carry over from one frame to the next until reset(), and each frame's compressed bytes
// start on a fresh 16-bit word. Built on libmspack's lzxd.
#pragma once

#include <cstdint>
#include <span>
#include <string>

struct lzxd_stream;

namespace x360tool {

class LzxFrameDecoder {
 public:
  static constexpr uint32_t kFrameSize = 32768;

  explicit LzxFrameDecoder(uint32_t window_size);  // a power of two, 32 KiB to 2 MiB
  ~LzxFrameDecoder();
  LzxFrameDecoder(const LzxFrameDecoder&) = delete;
  LzxFrameDecoder& operator=(const LzxFrameDecoder&) = delete;

  bool ok() const { return lzx_ != nullptr; }
  // Starts a new stream (a new window, trees and header).
  bool reset();
  // Decodes the frame in `in` into all of `out` (a full frame, or less for a stream's last one).
  bool decode(std::span<const uint8_t> in, std::span<uint8_t> out, std::string& error);

  struct Buffer {
    uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
  };

 private:
  int window_bits_ = 0;
  Buffer in_, out_;
  void* system_ = nullptr;  // mspack_system
  lzxd_stream* lzx_ = nullptr;
};

}  // namespace x360tool
