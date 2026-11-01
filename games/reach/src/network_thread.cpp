// Halo: Reach: the NETWORK_RECEIVE thread's idle loop.
//
// The thread (started at 8242C238, its loop 82441408) runs until the game shuts down:
//
//   while (state != done) {
//     if (the network is up) { lock; receive (822A5D28); unlock; }
//     Sleep(0);
//   }
//
// The receive step waits in select() for up to 15 ms on the game's sockets (822A4C90), but only when
// it has sockets open: at the menus, in campaign and with System Link off it has none, and the loop
// is Sleep(0) after Sleep(0). On the console that thread has a hardware thread to itself; here it
// took 90-100% of a host core all the time. The Sleep(0) at the end of that loop sleeps for a
// millisecond instead: nothing is waiting to be received when the loop gets there, and data that
// arrives meanwhile waits in the socket's buffer for the next select().
#include <chrono>
#include <thread>

#include "x360/hle.h"

namespace {
constexpr uint32_t kReceiveLoopSleepReturn = 0x824414D0;  // after the loop's "bl Sleep"
}

X360_GUEST_ORIGINAL(827E14A0)

// Sleep(milliseconds), XAPI's: SleepEx(milliseconds, FALSE).
PPC_FUNC(sub_827E14A0) {
  if (ctx.r3.u32 == 0 && uint32_t(ctx.lr) == kReceiveLoopSleepReturn) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }
  __imp__sub_827E14A0(ctx, base);
}
