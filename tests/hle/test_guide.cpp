// The Guide and the dashboard (docs/DASHBOARD.md): what a title is told while a system UI is open
// (XN_SYS_UIVISIBLE once for the Guide and what it opens over itself, XN_SYS_NUIPAUSE first in a
// Kinect title, XamIsUIActive), as a title's notification listener reads it; and the --window
// argument the launcher passes.
#include <cstdio>
#include <optional>

#include "x360/dashboard.h"
#include "x360/hle.h"
#include "x360/kernel.h"
#include "x360/memory.h"
#include "x360/settings.h"

PPC_EXTERN_FUNC(__imp__XamNotifyCreateListener);
PPC_EXTERN_FUNC(__imp__XNotifyGetNext);
PPC_EXTERN_FUNC(__imp__XamIsUIActive);

PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

int main() {
  using namespace x360;
  using kernel::SystemUi;

  // --window x,y,w,h
  {
    using dashboard::ParseWindowArg;
    using dashboard::WindowRect;
    CHECK(ParseWindowArg("100,50,1280,720") == (WindowRect{100, 50, 1280, 720}));
    CHECK(ParseWindowArg("-1920,0,1920,1080") == (WindowRect{-1920, 0, 1920, 1080}));  // a monitor on the left
    CHECK(!ParseWindowArg("100,50,1280"));
    CHECK(!ParseWindowArg("100,50,1280,720,1"));
    CHECK(!ParseWindowArg("a,b,c,d"));
    CHECK(!ParseWindowArg("0,0,0,0"));      // nothing to show in
    CHECK(!ParseWindowArg("0,0,1280,-720"));
    CHECK(!ParseWindowArg(""));
    CHECK(!ParseWindowArg("0,,1280,720"));
    CHECK(dashboard::FormatWindowArg({-8, 12, 1600, 900}) == "-8,12,1600,900");
    CHECK(ParseWindowArg(dashboard::FormatWindowArg({-8, 12, 1600, 900})) == (WindowRect{-8, 12, 1600, 900}));
    CHECK(dashboard::EventName(1234, L"ready") == L"Local\\AnyX360.Dashboard.1234.ready");
  }

  // A title's listener for the system's notifications (area 0).
  CHECK(kernel::InitMemory());
  alignas(64) PPCContext ctx{};
  uint8_t* base = kernel::g_base;
  const uint32_t out = kernel::SystemAlloc(16, 16);
  CHECK(out != 0);
  ctx.r3.u64 = 1, ctx.r4.u64 = 0;
  __imp__XamNotifyCreateListener(ctx, base);
  const uint32_t listener = ctx.r3.u32;
  CHECK(listener != 0);
  auto next = [&](uint32_t& id, uint32_t& param) {
    ctx.r3.u64 = listener, ctx.r4.u64 = 0, ctx.r5.u64 = out, ctx.r6.u64 = out + 4;
    __imp__XNotifyGetNext(ctx, base);
    id = kernel::Load<uint32_t>(out), param = kernel::Load<uint32_t>(out + 4);
    return ctx.r3.u32 != 0;
  };
  auto active = [&] {
    __imp__XamIsUIActive(ctx, base);
    return ctx.r3.u32;
  };
  uint32_t id = 0, param = 0;
  while (next(id, param)) {}  // the sign-ins and storage a new listener is told of
  constexpr uint32_t kSystemUi = 0x00000009, kNuiPause = 0x0006001A;

  // The Guide opens: the system UI is visible, the title pauses.
  CHECK(active() == 0);
  kernel::SetSystemUiOpen(SystemUi::kGuide, true);
  CHECK(next(id, param) && id == kSystemUi && param == 1);
  CHECK(!next(id, param));  // not a Kinect title: no pause gesture
  CHECK(active() == 1 && kernel::IsSystemUiOpen(SystemUi::kGuide));
  // The achievements list opened from it, then closed: still one system UI, open.
  kernel::SetSystemUiOpen(SystemUi::kAchievements, true);
  CHECK(!next(id, param));
  kernel::SetSystemUiOpen(SystemUi::kAchievements, false);
  CHECK(!next(id, param) && active() == 1);
  // Opened twice is still once.
  kernel::SetSystemUiOpen(SystemUi::kGuide, true);
  CHECK(!next(id, param));
  // Resume: the system UI closes.
  kernel::SetSystemUiOpen(SystemUi::kGuide, false);
  CHECK(next(id, param) && id == kSystemUi && param == 0);
  CHECK(!next(id, param) && active() == 0);

  // A player signed out from the Guide's Profiles: told while the system UI stays open.
  kernel::SetSystemUiOpen(SystemUi::kGuide, true);
  CHECK(next(id, param) && id == kSystemUi && param == 1);
  kernel::SignInUser(1);
  CHECK(next(id, param) && id == 0x0000000A && (param & 2));  // XN_SYS_SIGNINCHANGED, no UI closing
  kernel::SignOutUser(1);
  CHECK(next(id, param) && id == 0x0000000A && !(param & 2));
  CHECK(!next(id, param) && active() == 1);
  kernel::SetSystemUiOpen(SystemUi::kGuide, false);
  CHECK(next(id, param) && id == kSystemUi && param == 0);

  // In a Kinect title the Guide is first the pause gesture of player 1, as the Kinect Guide.
  settings::Override([](settings::Settings& s) { s.kinect.enabled = true; });
  kernel::SetSystemUiOpen(SystemUi::kGuide, true);
  CHECK(next(id, param) && id == kNuiPause && param == 0);
  CHECK(next(id, param) && id == kSystemUi && param == 1);
  // The guide gesture while it is open: paused already.
  kernel::SetKinectGuideOpen(true);
  CHECK(!next(id, param));
  kernel::SetSystemUiOpen(SystemUi::kGuide, false);
  CHECK(!next(id, param) && active() == 1);  // the Kinect Guide still is
  kernel::SetKinectGuideOpen(false);
  CHECK(next(id, param) && id == kSystemUi && param == 0);
  // The Kinect Guide alone, as before.
  kernel::SetKinectGuideOpen(true);
  CHECK(next(id, param) && id == kNuiPause);
  CHECK(next(id, param) && id == kSystemUi && param == 1);
  kernel::SetKinectGuideOpen(false);
  CHECK(next(id, param) && id == kSystemUi && param == 0);
  CHECK(!next(id, param) && active() == 0);

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
