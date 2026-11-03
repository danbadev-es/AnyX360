// What the game's messages hold when one can't be read. Diablo III stops itself on purpose then
// (sub_826D6D18 asserts, sub_82CDEAB0 stores to address 0), and without this the log doesn't say
// which message it was (docs/DIABLO3.md). It changes nothing the game does.
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>

#include "x360/hle.h"
#include "x360/kernel.h"

namespace {

constexpr uint32_t kExpectedId = 0x83281044;  // what sub_82967408 compares the message's ID with
constexpr uint32_t kMaxDumped = 512;
constexpr int kMaxFailures = 20;  // logged in full

// ANYX360_D3_MESSAGE_TRACE=1 logs each message too, with its first bytes.
const bool g_trace = std::getenv("ANYX360_D3_MESSAGE_TRACE") != nullptr;

void Dump(uint8_t* base, uint32_t message, uint32_t size) {
  constexpr uint32_t kPerLine = 32;
  // The header (ID, size) and the payload, read as the game read them.
  const uint32_t total = 12 + std::min(size, kMaxDumped);
  std::string line;
  for (uint32_t i = 0; i < total; ++i) {
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X ", PPC_LOAD_U8(message + i));
    line += hex;
    if ((i + 1) % kPerLine == 0 || i + 1 == total) {
      KLOG_W("  d3 message +%03X: %s", i / kPerLine * kPerLine, line.c_str());
      line.clear();
    }
  }
}

}  // namespace

// Reads a message of a type (r3) from its bytes (r4) into a decoded one (r5): 0 if the message's ID
// isn't the expected one or its payload doesn't decode.
X360_GUEST_ORIGINAL(82967408)
PPC_FUNC(sub_82967408) {
  static int failures = 0;
  const uint32_t type = ctx.r3.u32, message = ctx.r4.u32, caller = ctx.lr;
  __imp__sub_82967408(ctx, base);
  const bool failed = ctx.r3.u32 == 0;
  if (!failed && !g_trace) return;
  if (failed && ++failures > kMaxFailures) return;
  const uint32_t size = PPC_LOAD_U32(message + 8);
  // Its frame, just released: 128 bytes below r1, the ID it got at +80.
  const uint32_t id = PPC_LOAD_U32(ctx.r1.u32 - 128 + 80), expected = PPC_LOAD_U32(kExpectedId);
  KLOG_W("d3 message: type %u from %08X %s: at %08X, %u bytes, ID %08X (expected %08X)", type, caller,
         failed ? "COULD NOT BE READ" : "read", message, size, id, expected);
  Dump(base, message, failed ? size : std::min<uint32_t>(size, 32));
}
