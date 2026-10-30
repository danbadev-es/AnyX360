// Behaviour of Harmonix's Milo engine that the runtime doesn't emulate generically. The addresses
// are the title update's (0.0.1.5); games/dc3/notes/tu_addresses.md has the disc's.
#include "x360/hle.h"

PPC_EXTERN_FUNC(sub_827FD230);  // SetUnhandledExceptionFilter
PPC_EXTERN_FUNC(sub_82312478);  // Milo's exception filter: restores the previous filter, runs App::Run

namespace {

constexpr uint32_t kApp = 0x82D5C844;            // App* the filter passes to App::Run
constexpr uint32_t kPreviousFilter = 0x82D5C854;  // filter to restore

}  // namespace

// App::Run (0x823124A0) installs an unhandled-exception filter, then stores to address 0 on purpose:
// the filter, called for that exception, runs the game's main loop (0x82311D70) and never returns.
// There is no guest exception dispatch, so do what the exception would: call the filter directly.
X360_GUEST_ORIGINAL(823124A0)
PPC_FUNC(sub_823124A0) {
  ::x360::hle::guest_function_called(0x823124A0, "App::Run (exception filter start)", ctx);
  PPC_STORE_U32(kApp, ctx.r3.u32);
  ctx.r3.u64 = 0x82312478;
  ctx.lr = 0x823124C4;
  sub_827FD230(ctx, base);
  PPC_STORE_U32(kPreviousFilter, ctx.r3.u32);
  ctx.lr = 0x823124D0;
  sub_82312478(ctx, base);
}
