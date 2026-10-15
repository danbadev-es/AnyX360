// LZX decompression for "normal" compressed XEX images, using libmspack's lzxd over in-memory
// buffers (the same approach Xenia uses).
#include <cstdarg>
#include <cstdlib>
#include <cstring>

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

}  // namespace x360tool
