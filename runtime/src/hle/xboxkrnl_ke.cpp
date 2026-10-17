// xboxkrnl Ke* kernel exports that need no kernel objects yet.
#include "x360/hle.h"

#include <cstdio>

namespace x360::hle {

namespace {

// 1 = title process (as opposed to the system process).
uint32_t KeGetCurrentProcessType() { return 1; }

// The Xenon time base ticks at 50 MHz.
uint64_t KeQueryPerformanceFrequency() { return 50'000'000; }

// Floating-point state: HLE code never touches the guest's FP registers, so there is nothing to
// save, and exceptions stay masked as on the host.
uint32_t KeEnableFpuExceptions(uint32_t enable) {
  (void)enable;
  return 0;
}
uint32_t KeSaveFloatingPointState(uint32_t state) {
  (void)state;
  return 0;
}
uint32_t KeRestoreFloatingPointState(uint32_t state) {
  (void)state;
  return 0;
}

// A title's breakpoint (debug builds left in): log it and carry on, as a console without a
// debugger attached does.
void DbgBreakPoint() { std::fprintf(stderr, "[hle] DbgBreakPoint (continuing)\n"); }

}  // namespace

}  // namespace x360::hle

using namespace x360::hle;

X360_EXPORT(KeGetCurrentProcessType, KeGetCurrentProcessType)
X360_EXPORT(KeQueryPerformanceFrequency, KeQueryPerformanceFrequency)
X360_EXPORT(KeEnableFpuExceptions, KeEnableFpuExceptions)
X360_EXPORT(KeSaveFloatingPointState, KeSaveFloatingPointState)
X360_EXPORT(KeRestoreFloatingPointState, KeRestoreFloatingPointState)
X360_EXPORT(DbgBreakPoint, DbgBreakPoint)
