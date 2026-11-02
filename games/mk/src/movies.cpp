// Mortal Kombat Komplete Edition: the movie stream's reader no longer holds its lock while it waits.
//
// The story mode's cinematics are WMV files (Movies\mk001_002_003.wmv, 165 MB) played by the XDK's
// XMedia, whose decoder threads read the file through one game stream object (pointer at 83440D40,
// its critical section at 83440D44). The stream keeps a ring of 2 MB blocks that an I/O thread
// (82AF9078) fills from the queue at 83440D30; each reader has its own position in the file (+24
// and +28: one for the audio, one for the video), and a block is refilled only once both
// positions have left it.
//
// The read function (82AF8EA8) takes the lock and calls 82AF8C40, which loops until the bytes
// asked for are in a block, waiting 10 ms at a time on the I/O queue's event (queue+176) between
// tries, still holding the lock. When one reader runs a whole ring ahead of the other (the video
// decoder falling behind on a busy machine), it waits for a block that can only be refilled once
// the other reader moves, and the other reader waits for the lock: the cinematic froze about 90 s
// into the story mode's introduction, with the picture and the sound stopped (2 of 3 tries while
// other programs loaded the machine).
//
// Here that wait lets the lock go when the waiting thread holds it once (taken by 82AF8EA8 only),
// so the other reader can catch up, and takes it back before the loop looks at the blocks again;
// 82AF8C40 reads every field again after its wait. Waits on any other object, or by a thread that
// doesn't hold the lock, are the original function.
#include "ppc_config.h"
#include "x360/hle.h"
#include "x360/kernel.h"

X360_GUEST_ORIGINAL(8255D678)

namespace {
constexpr uint32_t kIoQueue = 0x83440D30;     // the I/O thread's queue (its event at +176)
constexpr uint32_t kStreamLock = 0x83440D44;  // the stream's critical section
// The game's import thunks of RtlEnterCriticalSection and RtlLeaveCriticalSection.
constexpr uint32_t kEnterCriticalSection = 0x833756D4;
constexpr uint32_t kLeaveCriticalSection = 0x833756C4;
}  // namespace

// The game's wait (object whose first word is a handle, timeout in ms): returns whether it was
// signalled.
PPC_FUNC(sub_8255D678) {
  using x360::kernel::Load;
  const uint32_t queue = Load<uint32_t>(kIoQueue);
  x360::kernel::Thread* thread = x360::kernel::CurrentThread();
  const bool holds_lock = queue && ctx.r3.u32 == Load<uint32_t>(queue + 176) && thread &&
                          Load<uint32_t>(kStreamLock + 0x18) == thread->kthread &&
                          Load<int32_t>(kStreamLock + 0x14) == 1;
  if (!holds_lock) {
    __imp__sub_8255D678(ctx, base);
    return;
  }
  const PPCRegister object = ctx.r3, timeout = ctx.r4;
  ctx.r3.u64 = kStreamLock;
  PPC_CALL_INDIRECT_FUNC(kLeaveCriticalSection);
  ctx.r3 = object;
  ctx.r4 = timeout;
  __imp__sub_8255D678(ctx, base);
  const PPCRegister result = ctx.r3;
  ctx.r3.u64 = kStreamLock;
  PPC_CALL_INDIRECT_FUNC(kEnterCriticalSection);
  ctx.r3 = result;
}
