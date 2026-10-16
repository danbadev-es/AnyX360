// Crash reporting: a vectored exception handler that explains faults in guest terms (which
// recompiled function, which guest address, the guest thread's registers).
#include <csignal>
#include <cstdio>
#include <exception>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

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

LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
  DWORD code = info->ExceptionRecord->ExceptionCode;
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

}  // namespace

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

}  // namespace x360::kernel
