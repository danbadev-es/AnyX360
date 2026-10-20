// LZX decompression for "normal" compressed XEX images, using libmspack's lzxd over in-memory
// buffers (the same approach Xenia uses), and frame by frame as the console's LDI (lzx_frames.h).
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "lzx_frames.h"
#include "xex.h"

extern "C" {
#include <mspack.h>
#include <lzx.h>
}

namespace x360tool {

namespace {

struct MemFile {
  uint8_t* data;
  size_t size;
  size_t pos;
};

int mem_read(mspack_file* f, void* buf, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  if (bytes < 0) return -1;
  size_t n = std::min<size_t>(size_t(bytes), m->size - m->pos);
  std::memcpy(buf, m->data + m->pos, n);
  m->pos += n;
  return int(n);
}

int mem_write(mspack_file* f, void* buf, int bytes) {
  auto* m = reinterpret_cast<MemFile*>(f);
  if (bytes < 0) return -1;
  size_t n = std::min<size_t>(size_t(bytes), m->size - m->pos);
  std::memcpy(m->data + m->pos, buf, n);
  m->pos += n;
  return int(n);  // a short write makes lzxd fail, which is what we want on overflow
}

void* mem_alloc(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
void mem_free(void* p) { std::free(p); }
void mem_copy(void* src, void* dst, size_t bytes) { std::memmove(dst, src, bytes); }
void mem_message(mspack_file*, const char*, ...) {}

mspack_system make_system() {
  mspack_system sys{};
  sys.read = mem_read;
  sys.write = mem_write;
  sys.message = mem_message;
  sys.alloc = mem_alloc;
  sys.free = mem_free;
  sys.copy = mem_copy;
  return sys;
}

}  // namespace

bool lzx_decompress(const std::vector<uint8_t>& in, uint32_t window_size, std::vector<uint8_t>& out,
                    std::string& error) {
  if (window_size == 0 || (window_size & (window_size - 1)) != 0) {
    error = "window size is not a power of two";
    return false;
  }
  int window_bits = 0;
  while ((1u << window_bits) < window_size) ++window_bits;

  mspack_system sys = make_system();
  MemFile src{const_cast<uint8_t*>(in.data()), in.size(), 0};
  MemFile dst{out.data(), out.size(), 0};

  lzxd_stream* lzx = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&src), reinterpret_cast<mspack_file*>(&dst),
                               window_bits, 0, 0x8000, off_t(out.size()), 0);
  if (!lzx) {
    error = "lzxd_init failed (window bits " + std::to_string(window_bits) + ")";
    return false;
  }
  int rc = lzxd_decompress(lzx, off_t(out.size()));
  lzxd_free(lzx);
  if (rc != MSPACK_ERR_OK) {
    error = "decoder error " + std::to_string(rc);
    return false;
  }
  return true;
}

static_assert(sizeof(LzxFrameDecoder::Buffer) == sizeof(MemFile), "lzxd reads the buffers as MemFile");

LzxFrameDecoder::LzxFrameDecoder(uint32_t window_size) {
  while (window_bits_ < 32 && (1u << window_bits_) < window_size) ++window_bits_;
  if ((1u << window_bits_) != window_size || window_bits_ < 15 || window_bits_ > 21) return;
  system_ = new mspack_system(make_system());
  reset();
}

LzxFrameDecoder::~LzxFrameDecoder() {
  if (lzx_) lzxd_free(lzx_);
  delete static_cast<mspack_system*>(system_);
}

bool LzxFrameDecoder::reset() {
  if (!system_) return false;
  if (lzx_) lzxd_free(lzx_);
  lzx_ = lzxd_init(static_cast<mspack_system*>(system_), reinterpret_cast<mspack_file*>(&in_),
                   reinterpret_cast<mspack_file*>(&out_), window_bits_, 0, 4096, 0, 0);
  return lzx_ != nullptr;
}

bool LzxFrameDecoder::decode(std::span<const uint8_t> in, std::span<uint8_t> out, std::string& error) {
  if (!lzx_) {
    error = "no decoder";
    return false;
  }
  if (out.empty() || out.size() > kFrameSize) {
    error = "a frame is 1 to 32768 bytes";
    return false;
  }
  in_ = {const_cast<uint8_t*>(in.data()), in.size(), 0};
  out_ = {out.data(), out.size(), 0};
  // A fresh 16-bit word: what lzxd read ahead at the end of the last frame (padding it made up past
  // that frame's bytes) isn't part of this one.
  lzx_->i_ptr = lzx_->i_end = lzx_->inbuf;
  lzx_->bit_buffer = 0;
  lzx_->bits_left = 0;
  lzx_->input_end = 0;
  lzx_->error = MSPACK_ERR_OK;
  // The stream ends with this frame as far as lzxd knows, so it doesn't decode the next one ahead.
  lzxd_set_output_length(lzx_, lzx_->offset + off_t(out.size()));
  const int rc = lzxd_decompress(lzx_, off_t(out.size()));
  if (rc != MSPACK_ERR_OK || out_.pos != out.size()) {
    error = "decoder error " + std::to_string(rc);
    return false;
  }
  return true;
}

}  // namespace x360tool
