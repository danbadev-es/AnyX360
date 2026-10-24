// Crash reporting: a vectored exception handler that explains faults in guest terms (which
// recompiled function, which guest address, the guest thread's registers).
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

namespace x360::kernel {

namespace {

std::mutex g_crash_mutex;

void Symbolize(uint64_t address, char* out, size_t size) {
  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
    std::snprintf(out, size, "%s+0x%llX", symbol->Name, (unsigned long long)displacement);
  } else {
    std::snprintf(out, size, "%016llX", (unsigned long long)address);
  }
}

thread_local bool t_faults_handled = false;

LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
  DWORD code = info->ExceptionRecord->ExceptionCode;
  // A thread that handles its own faults (the sampling profiler's stack walks).
  if (t_faults_handled) return EXCEPTION_CONTINUE_SEARCH;
  // Only report fatal faults; let C++ exceptions and debugger events through.
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
      code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_BREAKPOINT &&
      code != STATUS_PRIVILEGED_INSTRUCTION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  std::lock_guard lock(g_crash_mutex);
  CONTEXT* c = info->ContextRecord;
  char where[300];
  Symbolize(c->Rip, where, sizeof(where));
  Thread* t = CurrentThread();
  std::fprintf(stderr, "\n=== CRASH: exception %08lX at %s (thread %X) ===\n", code, where, t ? t->thread_id : 0);
  if (code == EXCEPTION_ACCESS_VIOLATION) {
    uint64_t target = info->ExceptionRecord->ExceptionInformation[1];
    const char* kind = info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "read";
    if (g_base && target >= uint64_t(g_base) && target < uint64_t(g_base) + 0x100000000ull) {
      std::fprintf(stderr, "  guest %s at %08llX\n", kind, (unsigned long long)hle::guest_offset(target - uint64_t(g_base)));
    } else {
      std::fprintf(stderr, "  host %s at %016llX\n", kind, (unsigned long long)target);
    }
  }
  if (t && t->context) {
    PPCContext& g = *t->context;
    std::fprintf(stderr, "  guest lr %08llX  r1 %08X  r3 %08X  r4 %08X  r5 %08X  r31 %08X\n",
                 (unsigned long long)g.lr, g.r1.u32, g.r3.u32, g.r4.u32, g.r5.u32, g.r31.u32);
  }
  // Host stack (recompiled function names are sub_<guest address>).
  void* frames[24];
  USHORT n = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
  (void)n;
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = c->Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = c->Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = c->Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  CONTEXT copy = *c;
  for (int i = 0; i < 16; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &copy, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset) {
      break;
    }
    Symbolize(frame.AddrPC.Offset, where, sizeof(where));
    std::fprintf(stderr, "  #%02d %s\n", i, where);
  }
  std::fflush(stderr);
  std::_Exit(4);
}

}  // namespace

std::string GuestBacktrace(int max_frames) {
  Thread* t = CurrentThread();
  if (!t || !t->context) return "(no guest thread)";
  std::string out;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08llX", (unsigned long long)t->context->lr);
  out += buf;
  // Back chain at 0(r1); each function saves its caller's LR at -8 of the frame above it.
  uint32_t frame = t->context->r1.u32;
  for (int i = 0; i < max_frames && frame >= t->stack_limit && frame < t->stack_base; ++i) {
    uint32_t next = Load<uint32_t>(frame);
    if (next <= frame || next >= t->stack_base) break;
    uint32_t lr = Load<uint32_t>(next - 8);
    std::snprintf(buf, sizeof(buf), " %08X", lr);
    out += buf;
    frame = next;
  }
  return out;
}

void PrintHostThreadStack(const wchar_t* name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 entry{sizeof(entry)};
  for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                               entry.th32ThreadID);
    if (!thread) continue;
    PWSTR description = nullptr;
    bool match = SUCCEEDED(GetThreadDescription(thread, &description)) && description && !wcscmp(description, name);
    if (description) LocalFree(description);
    // Unwound while the thread is suspended, without allocating (it may hold the heap's lock), and
    // symbolized once it runs again.
    uint64_t pcs[32];
    int count = 0;
    if (match && ::SuspendThread(thread) != DWORD(-1)) {
      CONTEXT context{};
      context.ContextFlags = CONTEXT_FULL;
      if (GetThreadContext(thread, &context)) {
        while (count < 32 && context.Rip) {
          pcs[count++] = context.Rip;
          DWORD64 image_base = 0;
          PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
          if (!function) {  // a leaf function: the return address is at the top of the stack
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
            continue;
          }
          void* handler_data = nullptr;
          DWORD64 establisher = 0;
          RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data,
                           &establisher, nullptr);
        }
      }
      ::ResumeThread(thread);
    }
    ::CloseHandle(thread);
    if (!count) continue;
    std::lock_guard lock(g_crash_mutex);
    std::fprintf(stderr, "host stack of \"%ls\" (thread %lu):\n", name, entry.th32ThreadID);
    char where[300];
    for (int i = 0; i < count; ++i) {
      Symbolize(pcs[i], where, sizeof(where));
      std::fprintf(stderr, "  #%02d %s\n", i, where);
    }
    std::fflush(stderr);
  }
  ::CloseHandle(snapshot);
}

namespace {

void PrintHostStack(const char* why) {
  std::lock_guard lock(g_crash_mutex);
  std::fprintf(stderr, "\n=== FATAL: %s (thread %X) ===\n", why, CurrentThread() ? CurrentThread()->thread_id : 0);
  void* frames[32];
  USHORT n = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
  char where[300];
  for (USHORT i = 0; i < n; ++i) {
    Symbolize(uint64_t(frames[i]), where, sizeof(where));
    std::fprintf(stderr, "  #%02u %s\n", i, where);
  }
  std::fflush(stderr);
}

void InvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned line,
                      uintptr_t) {
  char msg[512];
  std::snprintf(msg, sizeof(msg), "CRT invalid parameter: %ls in %ls (%ls:%u)", expression ? expression : L"?",
                function ? function : L"?", file ? file : L"?", line);
  PrintHostStack(msg);
  std::_Exit(5);
}

// ---- write watchpoint (debugging aid) ---------------------------------------------------------
// ANYX360_WATCH_WRITE=<guest address>: a hardware data breakpoint (DR0, 4 bytes, writes) on every
// guest thread; each write is logged with the host instruction (the recompiled function) and the
// guest thread's lr.

uint64_t g_watch_host = 0;

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1))
    return EXCEPTION_CONTINUE_SEARCH;
  info->ContextRecord->Dr6 = 0;
  char where[300];
  Symbolize(info->ContextRecord->Rip, where, sizeof(where));
  Thread* t = CurrentThread();
  uint32_t value = load_be<uint32_t>(reinterpret_cast<uint8_t*>(g_watch_host));
  std::lock_guard lock(g_crash_mutex);
  std::fprintf(stderr, "[watch] write -> %08X at %s (thread %X, guest lr %08llX r3 %08X)\n", value, where,
               t ? t->thread_id : 0, t && t->context ? (unsigned long long)t->context->lr : 0ull,
               t && t->context ? t->context->r3.u32 : 0);
  return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

void ArmWriteWatch() {
  if (!g_watch_host) return;
  // Debug registers of a running thread can only be changed while it is suspended: do it from a
  // helper thread.
  HANDLE self = nullptr;
  ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &self, 0, FALSE,
                  DUPLICATE_SAME_ACCESS);
  HANDLE helper = ::CreateThread(nullptr, 0, [](void* param) -> DWORD {
    HANDLE target = static_cast<HANDLE>(param);
    ::SuspendThread(target);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    ::GetThreadContext(target, &c);
    c.Dr0 = g_watch_host;
    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);  // L0, write, 4 bytes
    ::SetThreadContext(target, &c);
    ::ResumeThread(target);
    return 0;
  }, self, 0, nullptr);
  ::WaitForSingleObject(helper, INFINITE);
  ::CloseHandle(helper);
  ::CloseHandle(self);
}

void HandleOwnFaultsOnThisThread() { t_faults_handled = true; }

void InstallCrashHandler() {
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  _set_invalid_parameter_handler(InvalidParameter);
  std::set_terminate([] {
    const char* what = "std::terminate";
    try {
      if (auto e = std::current_exception()) std::rethrow_exception(e);
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
    }
    PrintHostStack(what);
    std::_Exit(6);
  });
  std::signal(SIGABRT, [](int) {
    PrintHostStack("abort()");
    std::_Exit(7);
  });
  // Last in the chain: Xenia's handler resolves MMIO accesses and memory watches first.
  AddVectoredExceptionHandler(0, Handler);
}

void SetupWriteWatch() {
  if (const char* watch = std::getenv("ANYX360_WATCH_WRITE")) {
    uint32_t guest = uint32_t(std::strtoul(watch, nullptr, 16));
    g_watch_host = uint64_t(g_base) + hle::host_offset(guest);
    AddVectoredExceptionHandler(1, WatchHandler);
    std::fprintf(stderr, "[watch] watching writes to %08X\n", guest);
  }
}

}  // namespace x360::kernel
