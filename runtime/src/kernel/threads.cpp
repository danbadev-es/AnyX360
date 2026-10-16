// Guest threads. Each guest thread is a host thread with a large host stack (recompiled code
// recurses deeply), its own PPCContext and the guest structures games access through r13:
//
//   PCR (r13)   +0x000 TLS pointer, +0x030 PCR self pointer, +0x070/0x074 stack base/limit,
//               +0x100 current KTHREAD, +0x10C current CPU, +0x2A8 PRCB pointer
//   KTHREAD     +0x000 dispatcher header (signaled when the thread ends), +0x05C stack base,
//               +0x060 stack limit, +0x068 TLS slots, +0x14C thread id, +0x150 start address,
//               +0x160 last error
//   TLS block   [__declspec(thread) data from the XEX TLS header][KeTlsAlloc slots]
#include <unordered_map>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::kernel {

namespace {

constexpr uint32_t kPcrSize = 0x2D8;
constexpr uint32_t kKThreadSize = 0xAB0;
constexpr uint32_t kHostStackSize = 32 * 1024 * 1024;
constexpr uint32_t kStackRedZone = 0x100;

thread_local std::shared_ptr<Thread> t_current;

std::mutex g_threads_mutex;
std::unordered_map<uint32_t, std::weak_ptr<Thread>> g_threads;
uint32_t g_next_thread_id = 0x10;
uint32_t g_process = 0;  // fake X_KPROCESS
uint8_t g_next_cpu = 0;

void SetHostThreadName(const char* name) {
  wchar_t wide[64];
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64);
  SetThreadDescription(GetCurrentThread(), wide);
}

// Allocates and fills the guest-side structures of a thread.
bool SetupGuestThread(Thread& t, uint32_t stack_size, uint8_t cpu) {
  const LoadedTitle& title = Title();
  stack_size = (std::max<uint32_t>(stack_size, 0x10000) + 0xFFF) & ~0xFFFu;

  uint32_t stack = HeapSystem().Alloc(stack_size + 0x1000);  // + guard-ish slack
  if (!stack) return false;
  std::memset(Ptr(stack), 0, stack_size + 0x1000);
  t.stack_limit = stack + 0x1000;
  t.stack_base = stack + 0x1000 + stack_size;

  uint32_t tls_size = title.tls_slot_count * 4 + title.tls_data_size;
  t.tls_static = SystemAlloc(std::max<uint32_t>(tls_size, 16));
  t.tls_dynamic = t.tls_static + title.tls_data_size;
  if (title.tls_raw_size) std::memcpy(Ptr(t.tls_static), Ptr(title.tls_raw_address), title.tls_raw_size);

  t.kthread = SystemAlloc(kKThreadSize);
  t.guest = t.kthread;
  t.pcr = SystemAlloc(kPcrSize);
  t.cpu = cpu;
  if (!t.kthread || !t.pcr || !t.tls_static) return false;

  if (!g_process) {
    g_process = SystemAlloc(0x60);
    Store<uint8_t>(g_process + 0x18, 1);  // process type: title
  }

  InitializeDispatcher(t.kthread, kThread, 0);
  Store<uint32_t>(t.kthread + 0x10, t.kthread + 0x10);  // mutant list (empty)
  Store<uint32_t>(t.kthread + 0x14, t.kthread + 0x10);
  Store<uint32_t>(t.kthread + 0x5C, t.stack_base);
  Store<uint32_t>(t.kthread + 0x60, t.stack_limit);
  Store<uint32_t>(t.kthread + 0x64, t.stack_base - 240);
  Store<uint32_t>(t.kthread + 0x68, t.tls_dynamic);
  Store<uint8_t>(t.kthread + 0x73, 1);  // process type: title
  Store<uint32_t>(t.kthread + 0x84, g_process);
  Store<uint8_t>(t.kthread + 0xBF, cpu);
  Store<uint32_t>(t.kthread + 0x9C, 0xFDFFD7FF);  // msr mask
  Store<uint32_t>(t.kthread + 0x14C, t.thread_id);
  Store<uint32_t>(t.kthread + 0x150, t.start_address);

  Store<uint32_t>(t.pcr + 0x000, t.tls_static);
  Store<uint64_t>(t.pcr + 0x030, t.pcr);
  Store<uint32_t>(t.pcr + 0x070, t.stack_base);
  Store<uint32_t>(t.pcr + 0x074, t.stack_limit);
  Store<uint32_t>(t.pcr + 0x100, t.kthread);
  Store<uint8_t>(t.pcr + 0x10C, cpu);
  Store<uint32_t>(t.pcr + 0x2A8, t.pcr + 0x100);
  return true;
}

void InitContext(Thread& t, PPCContext& ctx) {
  ctx.r1.u64 = t.stack_base - kStackRedZone;
  ctx.r13.u64 = t.pcr;
  ctx.fpscr.loadFromHost();
  t.context = &ctx;
}

void FinishThread(Thread& t, uint32_t exit_code) {
  t.exit_code = exit_code;
  {
    std::lock_guard lock(DispatcherLock());
    Ptr<DispatchHeader>(t.kthread)->signal_state = 1;
    NotifyDispatcherChange();
  }
  KLOG_I("thread %X exited with %08X", t.thread_id, exit_code);
}

void WaitWhileSuspended(Thread& t) {
  std::unique_lock lock(t.suspend_mutex);
  t.suspend_cv.wait(lock, [&] { return t.suspend_count <= 0; });
}

DWORD WINAPI GuestThreadMain(void* param) {
  auto* holder = static_cast<std::shared_ptr<Thread>*>(param);
  t_current = *holder;
  delete holder;
  Thread& t = *t_current;

  char name[32];
  std::snprintf(name, sizeof(name), "guest %X", t.thread_id);
  SetHostThreadName(name);

  alignas(64) PPCContext ctx{};
  InitContext(t, ctx);
  WaitWhileSuspended(t);

  if (t.host_body) {
    t.host_body();
    FinishThread(t, 0);
    return 0;
  }

  uint32_t function = t.start_address;
  if (t.xapi_startup) {
    ctx.r3.u64 = t.start_address;
    ctx.r4.u64 = t.start_context;
    function = t.xapi_startup;
  } else {
    ctx.r3.u64 = t.start_context;
  }
  PPCFunc* host = PPC_LOOKUP_FUNC(g_base, function);
  if (!host) Fatal("thread %X: no recompiled function at %08X", t.thread_id, function);
  KLOG_I("thread %X starting at %08X (arg %08X)", t.thread_id, function, uint32_t(ctx.r3.u64));
  host(ctx, g_base);
  FinishThread(t, ctx.r3.u32);
  return 0;
}

std::shared_ptr<Thread> NewThread(uint8_t cpu) {
  auto t = std::make_shared<Thread>();
  {
    std::lock_guard lock(g_threads_mutex);
    t->thread_id = g_next_thread_id++;
    g_threads[t->thread_id] = t;
  }
  (void)cpu;
  return t;
}

bool StartHostThread(const std::shared_ptr<Thread>& t) {
  auto* holder = new std::shared_ptr<Thread>(t);
  t->host = ::CreateThread(nullptr, kHostStackSize, GuestThreadMain, holder, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!t->host) {
    delete holder;
    return false;
  }
  return true;
}

uint8_t PickCpu(uint32_t creation_flags) {
  uint8_t mask = uint8_t(creation_flags >> 24);
  if (mask) {
    for (uint8_t i = 0; i < 6; ++i)
      if (mask & (1u << i)) return i;
  }
  Thread* parent = CurrentThread();
  if (parent) return parent->cpu;
  return g_next_cpu = uint8_t((g_next_cpu + 1) % 6);
}

}  // namespace

Thread* CurrentThread() { return t_current.get(); }
std::shared_ptr<Thread> CurrentThreadShared() { return t_current; }

std::shared_ptr<Thread> ThreadById(uint32_t id) {
  std::lock_guard lock(g_threads_mutex);
  auto it = g_threads.find(id);
  return it == g_threads.end() ? nullptr : it->second.lock();
}

uint32_t CreateThread(uint32_t stack_size, uint32_t xapi_startup, uint32_t start_address, uint32_t start_context,
                      uint32_t creation_flags, std::shared_ptr<Thread>* out) {
  uint8_t cpu = PickCpu(creation_flags);
  auto t = NewThread(cpu);
  t->start_address = start_address;
  t->start_context = start_context;
  t->xapi_startup = xapi_startup;
  t->suspend_count = (creation_flags & 0x1) ? 1 : 0;
  if (!stack_size) stack_size = Title().stack_size;
  if (!SetupGuestThread(*t, stack_size, cpu)) return X_STATUS_NO_MEMORY;
  t->handle = CreateHandle(t);
  if (!StartHostThread(t)) return X_STATUS_NO_MEMORY;
  if (out) *out = t;
  return X_STATUS_SUCCESS;
}

uint32_t RunMainThread(uint32_t entry_point, uint32_t stack_size) {
  std::shared_ptr<Thread> main;
  uint32_t status = CreateThread(stack_size, 0, entry_point, 0, 0, &main);
  if (status != X_STATUS_SUCCESS) return status;
  WaitForSingleObject(main->host, INFINITE);
  return main->exit_code;
}

std::shared_ptr<Thread> CreateHostDrivenThread(const char* name, uint8_t cpu, std::function<void()> body) {
  auto t = NewThread(cpu);
  t->host_body = std::move(body);
  t->name = name;
  if (!SetupGuestThread(*t, 0x40000, cpu)) return nullptr;
  t->handle = CreateHandle(t);
  if (!StartHostThread(t)) return nullptr;
  return t;
}

uint64_t CallGuest(uint32_t function, std::initializer_list<uint64_t> args) {
  Thread* t = CurrentThread();
  if (!t || !t->context) Fatal("CallGuest(%08X) outside a guest thread", function);
  PPCFunc* host = PPC_LOOKUP_FUNC(g_base, function);
  if (!host) Fatal("CallGuest: no recompiled function at %08X", function);

  PPCContext& ctx = *t->context;
  // The callee may clobber volatile registers the interrupted guest code still needs; keep a copy.
  auto* saved = static_cast<PPCContext*>(_aligned_malloc(sizeof(PPCContext), 64));
  std::memcpy(saved, &ctx, sizeof(PPCContext));

  PPCRegister* regs[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9, &ctx.r10};
  size_t i = 0;
  for (uint64_t a : args) {
    if (i < 8) regs[i++]->u64 = a;
  }
  // Leave room so the callee does not overwrite the caller's frame.
  ctx.r1.u64 = ctx.r1.u64 - 0x200;
  host(ctx, g_base);
  uint64_t result = ctx.r3.u64;

  std::memcpy(&ctx, saved, sizeof(PPCContext));
  _aligned_free(saved);
  return result;
}

[[noreturn]] void ExitCurrentThread(uint32_t exit_code) {
  Thread* t = CurrentThread();
  if (t) FinishThread(*t, exit_code);
  ExitThread(exit_code);
}

int ResumeThread(Thread& t) {
  std::lock_guard lock(t.suspend_mutex);
  int previous = t.suspend_count;
  if (t.suspend_count > 0) --t.suspend_count;
  t.suspend_cv.notify_all();
  return previous;
}

int SuspendThread(Thread& t) {
  std::lock_guard lock(t.suspend_mutex);
  int previous = t.suspend_count++;
  if (&t != CurrentThread()) KLOG_W("SuspendThread on a running thread %X is only honoured before it starts", t.thread_id);
  return previous;
}

}  // namespace x360::kernel
