// Bridge between XenonRecomp-generated code and C++ implementations of console OS exports.
//
// XenonRecomp turns every imported function thunk into a call to `__imp__<ExportName>(ctx, base)`
// (C++ linkage, see PPC_EXTERN_FUNC). The runtime provides those symbols:
//   - src/hle/stubs.cpp defines a *weak* stub for every known xboxkrnl/xam export that logs the
//     first call and returns 0;
//   - X360_EXPORT(Name, fn) defines the *strong* symbol, which replaces the stub at link time.
//
// `fn` is an ordinary C++ function. Arguments are read from r3..r10 (integers and guest pointers)
// and f1..f13 (floating point) in declaration order, following the Xbox 360 ABI; the return value
// goes to r3 or f1. Guest pointer parameters arrive as host pointers (nullptr for guest NULL).
//
// This header includes the game's generated ppc_config.h, so HLE code must be compiled per game
// (see anyx360_add_hle() in runtime/CMakeLists.txt): PPCContext's layout depends on that config.
#pragma once

#include "ppc_config.h"
#include <ppc_context.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

#include "x360/endian.h"

namespace x360::hle {

// Logs the first call of an unimplemented export (thread-safe).
void unimplemented(const char* name);
// Logs the first call of a replaced guest function: name, arguments r3-r6 and the caller.
void guest_function_called(uint32_t address, const char* name, const PPCContext& ctx);
// Records the export the current guest thread is executing (for thread dumps).
extern void (*g_note_import)(const char* name);

// Base of the guest address space, for converting host pointers back to guest addresses.
void set_guest_base(uint8_t* base);
uint8_t* guest_base();

// Offset of a guest address from the guest base. The 0xE0000000 window is hosted 4 KB higher so
// that it aliases physical memory as on the console (see PPC_ADDR in ppc_context.h).
inline uint64_t host_offset(uint32_t addr) { return uint64_t(addr) + (addr >= 0xE0000000u ? 0x1000u : 0u); }
inline uint32_t guest_offset(uint64_t offset) {
  return uint32_t(offset >= 0xE0001000ull ? offset - 0x1000 : offset);
}

inline uint32_t to_guest(const void* p) {
  return p ? guest_offset(uint64_t(static_cast<const uint8_t*>(p) - guest_base())) : 0;
}
template <typename T = uint8_t>
inline T* to_host(uint32_t addr) {
  return addr ? reinterpret_cast<T*>(guest_base() + host_offset(addr)) : nullptr;
}

namespace detail {

inline PPCRegister& gpr(PPCContext& ctx, int i) {
  switch (i) {
    case 0: return ctx.r3;
    case 1: return ctx.r4;
    case 2: return ctx.r5;
    case 3: return ctx.r6;
    case 4: return ctx.r7;
    case 5: return ctx.r8;
    case 6: return ctx.r9;
    default: return ctx.r10;
  }
}

inline PPCRegister& fpr(PPCContext& ctx, int i) {
  switch (i) {
    case 0: return ctx.f1;
    case 1: return ctx.f2;
    case 2: return ctx.f3;
    case 3: return ctx.f4;
    case 4: return ctx.f5;
    case 5: return ctx.f6;
    case 6: return ctx.f7;
    case 7: return ctx.f8;
    case 8: return ctx.f9;
    case 9: return ctx.f10;
    case 10: return ctx.f11;
    case 11: return ctx.f12;
    default: return ctx.f13;
  }
}

struct Cursor {
  int g = 0;
  int f = 0;
};

// Integer argument `index`: r3..r10, then 8-byte slots in the caller's parameter save area starting
// at r1 + 0x54 (the 9th argument). Callers store 32-bit arguments with stw at the start of the
// slot (seen in Gears of War 3's calls to VdInitializeScalerCommandBuffer), so read that word.
inline uint64_t int_arg(PPCContext& ctx, uint8_t* base, int index) {
  if (index < 8) return gpr(ctx, index).u64;
  return load_be<uint32_t>(base + ctx.r1.u32 + 0x54 + (index - 8) * 8);
}

template <typename T>
T read_arg(PPCContext& ctx, uint8_t* base, Cursor& c) {
  if constexpr (std::is_same_v<T, PPCContext*>) {
    return &ctx;  // raw access for variadic functions (does not consume an argument)
  } else if constexpr (std::is_floating_point_v<T>) {
    return static_cast<T>(fpr(ctx, c.f++).f64);
  } else if constexpr (std::is_pointer_v<T>) {
    uint32_t addr = uint32_t(int_arg(ctx, base, c.g++));
    return addr ? reinterpret_cast<T>(base + host_offset(addr)) : nullptr;
  } else {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "unsupported HLE argument type");
    return static_cast<T>(int_arg(ctx, base, c.g++));
  }
}

template <typename T>
inline constexpr bool is_fp = std::is_floating_point_v<T>;

template <typename R, typename... Args>
struct Signature {
  static constexpr int gprs = ((is_fp<Args> ? 0 : 1) + ... + 0);
  static constexpr int fprs = ((is_fp<Args> ? 1 : 0) + ... + 0);
};

template <typename R, typename... Args>
void call(R (*fn)(Args...), PPCContext& ctx, uint8_t* base) {
  static_assert(Signature<R, Args...>::fprs <= 13, "too many floating-point arguments");

  Cursor cursor;
  // Braced initialisation guarantees left-to-right evaluation, i.e. register order.
  std::tuple<Args...> args{read_arg<Args>(ctx, base, cursor)...};

  if constexpr (std::is_void_v<R>) {
    std::apply(fn, args);
  } else {
    R r = std::apply(fn, args);
    if constexpr (std::is_floating_point_v<R>) {
      ctx.f1.f64 = static_cast<double>(r);
    } else if constexpr (std::is_pointer_v<R>) {
      ctx.r3.u64 = r ? guest_offset(uint64_t(reinterpret_cast<const uint8_t*>(r) - base)) : 0;
    } else if constexpr (std::is_same_v<R, bool>) {
      ctx.r3.u64 = r ? 1 : 0;
    } else if constexpr (std::is_signed_v<R>) {
      ctx.r3.s64 = static_cast<int64_t>(r);
    } else {
      ctx.r3.u64 = static_cast<uint64_t>(r);
    }
  }
}

}  // namespace detail

template <auto Fn>
inline void invoke(PPCContext& ctx, uint8_t* base) {
  detail::call(Fn, ctx, base);
}

// Common NTSTATUS values.
enum : uint32_t {
  STATUS_SUCCESS = 0x00000000,
  STATUS_NOT_IMPLEMENTED = 0xC0000002,
  STATUS_INVALID_PARAMETER = 0xC000000D,
};

// Guest structures (big-endian layout).
struct ANSI_STRING {
  be<uint16_t> Length;
  be<uint16_t> MaximumLength;
  be<uint32_t> Buffer;
};
static_assert(sizeof(ANSI_STRING) == 8);

using UNICODE_STRING = ANSI_STRING;  // same layout; Buffer points to big-endian UTF-16

}  // namespace x360::hle

// Defines the strong implementation of an import, replacing its weak stub.
#define X360_EXPORT(name, fn)                                            \
  PPC_FUNC(__imp__##name) {                                              \
    if (::x360::hle::g_note_import) ::x360::hle::g_note_import(#name);   \
    ::x360::hle::invoke<fn>(ctx, base);                                  \
    if (::x360::hle::g_note_import) ::x360::hle::g_note_import(nullptr); \
  }

// Replacing functions of the recompiled title (for example a library linked into it). XenonRecomp
// emits each function as a weak sub_<address> aliasing __imp__sub_<address>, used by direct calls
// and the function table alike, so a strong sub_<address> takes over every caller. Each macro keeps
// a reference to __imp__sub_<address>: a wrong address is a link error, not an unused definition.
#define X360_GUEST_ORIGINAL(address)                \
  extern "C" PPC_FUNC(__imp__sub_##address);        \
  __attribute__((used)) static void (*const x360_original_sub_##address)(PPCContext&, uint8_t*) = \
      __imp__sub_##address;

// sub_<address> runs `fn`, called like an export (arguments from r3.., result to r3/f1).
#define X360_GUEST_FUNCTION(address, fn)                                 \
  X360_GUEST_ORIGINAL(address)                                           \
  PPC_FUNC(sub_##address) {                                              \
    ::x360::hle::guest_function_called(0x##address, #fn, ctx);           \
    if (::x360::hle::g_note_import) ::x360::hle::g_note_import(#fn);     \
    ::x360::hle::invoke<fn>(ctx, base);                                  \
    if (::x360::hle::g_note_import) ::x360::hle::g_note_import(nullptr); \
  }

// sub_<address> does nothing and returns `result` in r3.
#define X360_GUEST_STUB(address, name, result)                    \
  X360_GUEST_ORIGINAL(address)                                    \
  PPC_FUNC(sub_##address) {                                       \
    ::x360::hle::guest_function_called(0x##address, name, ctx);   \
    ctx.r3.u64 = uint64_t(result);                                \
  }

// sub_<address> logs its first call and runs the original code.
#define X360_GUEST_TRACE(address, name)                           \
  X360_GUEST_ORIGINAL(address)                                    \
  PPC_FUNC(sub_##address) {                                       \
    ::x360::hle::guest_function_called(0x##address, name, ctx);   \
    __imp__sub_##address(ctx, base);                              \
  }
