// xboxkrnl Ke* kernel exports that need no kernel objects yet.
#include "x360/hle.h"

namespace x360::hle {

namespace {

// 1 = title process (as opposed to the system process).
uint32_t KeGetCurrentProcessType() { return 1; }

// The Xenon time base ticks at 50 MHz.
uint64_t KeQueryPerformanceFrequency() { return 50'000'000; }

}  // namespace

}  // namespace x360::hle

using namespace x360::hle;

X360_EXPORT(KeGetCurrentProcessType, KeGetCurrentProcessType)
X360_EXPORT(KeQueryPerformanceFrequency, KeQueryPerformanceFrequency)
