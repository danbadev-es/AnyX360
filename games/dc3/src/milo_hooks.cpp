// Behaviour of Harmonix's Milo engine that the runtime doesn't emulate generically.
#include "x360/hle.h"

PPC_EXTERN_FUNC(sub_827FBC80);  // SetUnhandledExceptionFilter
PPC_EXTERN_FUNC(sub_82311A40);  // Milo's exception filter: restores the previous filter, runs App::Run

namespace {

constexpr uint32_t kApp = 0x82D5C784;            // App* the filter passes to App::Run
constexpr uint32_t kPreviousFilter = 0x82D5C794;  // filter to restore

}  // namespace

// App::Run (0x82311A68) installs an unhandled-exception filter, then stores to address 0 on purpose:
// the filter, called for that exception, runs the game's main loop (0x82311338) and never returns.
// There is no guest exception dispatch, so do what the exception would: call the filter directly.
X360_GUEST_ORIGINAL(82311A68)
PPC_FUNC(sub_82311A68) {
  ::x360::hle::guest_function_called(0x82311A68, "App::Run (exception filter start)", ctx);
  PPC_STORE_U32(kApp, ctx.r3.u32);
  ctx.r3.u64 = 0x82311A40;
  ctx.lr = 0x82311A8C;
  sub_827FBC80(ctx, base);
  PPC_STORE_U32(kPreviousFilter, ctx.r3.u32);
  ctx.lr = 0x82311A98;
  sub_82311A40(ctx, base);
}
