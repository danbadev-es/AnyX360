// xboxkrnl Rtl* runtime-library exports. Semantics follow the NT kernel and Xenia.
#include <cstring>

#include "x360/hle.h"

namespace x360::hle {

namespace {

void RtlInitAnsiString(ANSI_STRING* dst, const char* src) {
  if (!dst) return;
  if (src) {
    uint16_t len = uint16_t(std::strlen(src));
    dst->Length = len;
    dst->MaximumLength = uint16_t(len + 1);
  } else {
    dst->Length = 0;
    dst->MaximumLength = 0;
  }
  dst->Buffer = to_guest(src);
}

void RtlInitUnicodeString(UNICODE_STRING* dst, const uint16_t* src) {
  if (!dst) return;
  uint16_t chars = 0;
  if (src) {
    while (src[chars] != 0) ++chars;  // zero is zero in either byte order
    dst->Length = uint16_t(chars * 2);
    dst->MaximumLength = uint16_t(chars * 2 + 2);
  } else {
    dst->Length = 0;
    dst->MaximumLength = 0;
  }
  dst->Buffer = to_guest(src);
}

// Returns the number of leading bytes that match.
uint32_t RtlCompareMemory(const uint8_t* a, const uint8_t* b, uint32_t length) {
  uint32_t i = 0;
  while (i < length && a[i] == b[i]) ++i;
  return i;
}

// Returns the number of leading bytes made of the 32-bit `pattern`, compared as stored in guest memory.
uint32_t RtlCompareMemoryUlong(const uint8_t* src, uint32_t length, uint32_t pattern) {
  if ((reinterpret_cast<uintptr_t>(src) & 3) != 0) return 0;  // NT requires alignment
  uint32_t i = 0;
  for (; i + 4 <= length; i += 4) {
    if (load_be<uint32_t>(src + i) != pattern) break;
  }
  return i;
}

void RtlFillMemoryUlong(uint8_t* dst, uint32_t length, uint32_t pattern) {
  for (uint32_t i = 0; i + 4 <= length; i += 4) store_be<uint32_t>(dst + i, pattern);
}

uint8_t RtlUpperChar(uint8_t c) { return (c >= 'a' && c <= 'z') ? uint8_t(c - 'a' + 'A') : c; }
uint8_t RtlLowerChar(uint8_t c) { return (c >= 'A' && c <= 'Z') ? uint8_t(c - 'A' + 'a') : c; }

}  // namespace

}  // namespace x360::hle

using namespace x360::hle;

X360_EXPORT(RtlInitAnsiString, RtlInitAnsiString)
X360_EXPORT(RtlInitUnicodeString, RtlInitUnicodeString)
X360_EXPORT(RtlCompareMemory, RtlCompareMemory)
X360_EXPORT(RtlCompareMemoryUlong, RtlCompareMemoryUlong)
X360_EXPORT(RtlFillMemoryUlong, RtlFillMemoryUlong)
X360_EXPORT(RtlUpperChar, RtlUpperChar)
X360_EXPORT(RtlLowerChar, RtlLowerChar)
