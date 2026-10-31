// Behaviour of Harmonix's Milo engine that the runtime doesn't emulate generically.
#include "x360/hle.h"

PPC_EXTERN_FUNC(sub_826D9C28);  // SetUnhandledExceptionFilter
PPC_EXTERN_FUNC(sub_822A04F8);  // Milo's exception filter: restores the previous filter, runs App::Run

namespace {

constexpr uint32_t kApp = 0x82CEF5C0;            // App* the filter passes to App::Run
constexpr uint32_t kPreviousFilter = 0x82CEF5D0;  // filter to restore

}  // namespace

// App::Run (0x822A0520) installs an unhandled-exception filter, then stores to address 0 on purpose:
// the filter, called for that exception, runs the game's main loop (0x822A01C0) and never returns.
// There is no guest exception dispatch, so do what the exception would: call the filter directly.
// The same code as Dance Central 3's (games/dc3/src/milo_hooks.cpp).
X360_GUEST_ORIGINAL(822A0520)
PPC_FUNC(sub_822A0520) {
  ::x360::hle::guest_function_called(0x822A0520, "App::Run (exception filter start)", ctx);
  PPC_STORE_U32(kApp, ctx.r3.u32);
  ctx.r3.u64 = 0x822A04F8;
  ctx.lr = 0x822A0544;
  sub_826D9C28(ctx, base);
  PPC_STORE_U32(kPreviousFilter, ctx.r3.u32);
  ctx.lr = 0x822A0550;
  sub_822A04F8(ctx, base);
}
