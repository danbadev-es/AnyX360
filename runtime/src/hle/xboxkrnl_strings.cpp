// String formatting (DbgPrint, sprintf family, vswprintf) and Rtl string/runtime helpers.
#include <cwctype>

#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace x360::kernel;

namespace {

// ---- guest printf ------------------------------------------------------------------------------

// Arguments come from GPRs (then the stack) for variadic calls, or from a va_list (8-byte slots).
struct ArgSource {
  PPCContext* ctx = nullptr;
  int index = 0;           // next register argument
  uint32_t va_list_ptr = 0;  // when non-zero, read from here instead

  uint64_t Next() {
    if (va_list_ptr) {
      uint64_t v = Load<uint64_t>(va_list_ptr);
      va_list_ptr += 8;
      return v;
    }
    return x360::hle::detail::int_arg(*ctx, g_base, index++);
  }
};

// Formats like the console's CRT. Char is char for the sprintf family and char16_t for the wide one
// (vswprintf), whose format and result are UTF-16 and whose %s and %c take wide arguments by default
// (MSVC rules: %S and %C swap, h always means narrow and l wide).
template <typename Char>
std::basic_string<Char> FormatGuest(const std::basic_string<Char>& format, ArgSource args) {
  constexpr bool kWideFormat = sizeof(Char) > 1;
  using String = std::basic_string<Char>;
  auto is_digit = [](Char c) { return c >= '0' && c <= '9'; };
  // Text of a host conversion (ASCII), in the output's characters.
  auto widen = [](const char* text) { return String(text, text + std::strlen(text)); };
  String out;
  const Char* f = format.c_str();
  while (*f) {
    if (*f != '%') {
      out += *f++;
      continue;
    }
    const Char* spec_start = f++;
    if (*f == '%') {
      out += Char('%');
      ++f;
      continue;
    }
    std::string flags;
    while (*f && *f < 0x80 && std::strchr("-+ #0", char(*f))) flags += char(*f++);
    int width = -1, precision = -1;
    if (*f == '*') {
      width = int(int32_t(args.Next()));
      ++f;
    } else if (is_digit(*f)) {
      width = 0;
      while (is_digit(*f)) width = width * 10 + (*f++ - '0');
    }
    if (*f == '.') {
      ++f;
      precision = 0;
      if (*f == '*') {
        precision = int(int32_t(args.Next()));
        ++f;
      } else {
        while (is_digit(*f)) precision = precision * 10 + (*f++ - '0');
      }
    }
    // Length modifiers.
    bool is_long_long = false, is_short = false, is_wide = false;
    for (;;) {
      if (*f == 'h') { is_short = true; ++f; }
      else if (*f == 'l') { if (f[1] == 'l') { is_long_long = true; f += 2; } else { is_wide = true; ++f; } }
      else if (*f == 'L' || *f == 'z' || *f == 't' || *f == 'j') { ++f; }
      else if (f[0] == 'I' && f[1] == '6' && f[2] == '4') { is_long_long = true; f += 3; }
      else if (f[0] == 'I' && f[1] == '3' && f[2] == '2') { f += 3; }
      else break;
    }
    char conv = *f && *f < 0x80 ? char(*f++) : 0;

    std::string spec = "%" + flags;
    if (width >= 0) spec += std::to_string(width);
    if (precision >= 0) spec += "." + std::to_string(precision);

    char buf[512];
    switch (conv) {
      case 'd':
      case 'i': {
        int64_t v = int64_t(args.Next());
        if (!is_long_long) v = is_short ? int16_t(v) : int32_t(v);
        std::snprintf(buf, sizeof(buf), (spec + "lld").c_str(), (long long)v);
        out += widen(buf);
        break;
      }
      case 'u':
      case 'x':
      case 'X':
      case 'o': {
        uint64_t v = args.Next();
        if (!is_long_long) v = is_short ? uint16_t(v) : uint32_t(v);
        std::snprintf(buf, sizeof(buf), (spec + "ll" + conv).c_str(), (unsigned long long)v);
        out += widen(buf);
        break;
      }
      case 'p':
        std::snprintf(buf, sizeof(buf), "%08X", uint32_t(args.Next()));
        out += widen(buf);
        break;
      case 'c':
      case 'C': {
        uint64_t v = args.Next();
        const bool wide = is_wide || (!is_short && kWideFormat == (conv == 'c'));
        if constexpr (kWideFormat) {
          out += wide ? Char(uint16_t(v)) : Char(uint8_t(v));
        } else {
          out += wide ? char(uint16_t(v)) : char(v);
        }
        break;
      }
      case 'f':
      case 'F':
      case 'e':
      case 'E':
      case 'g':
      case 'G':
      case 'a':
      case 'A': {
        uint64_t bits = args.Next();
        double d;
        std::memcpy(&d, &bits, 8);
        std::snprintf(buf, sizeof(buf), (spec + conv).c_str(), d);
        out += widen(buf);
        break;
      }
      case 's':
      case 'S': {
        uint32_t p = uint32_t(args.Next());
        // In sprintf %s / %hs / %hS are narrow and %ls / %S / %lS wide; vswprintf swaps s and S.
        const bool wide = is_wide || (!is_short && kWideFormat == (conv == 's'));
        String s;
        if (!p) {
          s = widen("(null)");
        } else if (wide) {
          for (uint32_t a = p; uint16_t c = Load<uint16_t>(a); a += 2) {
            if constexpr (kWideFormat) s += Char(c);
            else s += c < 0x80 ? char(c) : '?';
          }
        } else {
          for (const char* c = Ptr<const char>(p); *c; ++c) s += Char(uint8_t(*c));
        }
        if (precision >= 0 && s.size() > size_t(precision)) s.resize(precision);
        if (width > int(s.size())) {
          if (flags.find('-') != std::string::npos) s.append(width - s.size(), Char(' '));
          else s.insert(0, width - s.size(), Char(' '));
        }
        out += s;
        break;
      }
      case 'n':
        args.Next();
        break;
      default:
        out.append(spec_start, f);
        break;
    }
  }
  return out;
}

std::string GuestFormat(uint32_t format_ptr, ArgSource args) {
  const char* format = Ptr<const char>(format_ptr);
  return format ? FormatGuest<char>(format, args) : std::string();
}

// The wide functions' format is big-endian UTF-16 in guest memory.
std::u16string GuestFormatWide(uint32_t format_ptr, ArgSource args) {
  std::u16string format;
  for (uint32_t a = format_ptr; a && Load<uint16_t>(a); a += 2) format += char16_t(Load<uint16_t>(a));
  return FormatGuest<char16_t>(format, args);
}

// Copies a formatted result into a guest buffer with _snprintf semantics.
int32_t StoreFormatted(uint32_t buffer, int64_t count, const std::string& s) {
  if (count < 0) {  // unbounded (sprintf)
    std::memcpy(Ptr(buffer), s.c_str(), s.size() + 1);
    return int32_t(s.size());
  }
  if (count == 0) return -1;
  if (s.size() < size_t(count)) {
    std::memcpy(Ptr(buffer), s.c_str(), s.size() + 1);
    return int32_t(s.size());
  }
  std::memcpy(Ptr(buffer), s.data(), size_t(count));  // no terminator when truncated
  return -1;
}

void LogGuest(const std::string& text) {
  std::string s = text;
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  if (!s.empty()) KLOG_I("[guest] %s", s.c_str());
}

// ---- Rtl helpers ------------------------------------------------------------------------------

uint32_t RtlImageXexHeaderField(uint32_t header, uint32_t field) {
  if (!header) return 0;
  uint32_t count = Load<uint32_t>(header + 0x14);
  for (uint32_t i = 0; i < count && i < 0x200; ++i) {
    uint32_t entry = header + 0x18 + i * 8;
    if (Load<uint32_t>(entry) != field) continue;
    uint32_t value = Load<uint32_t>(entry + 4);
    switch (field & 0xFF) {
      case 0x00: return value;
      case 0x01: return entry + 4;
      default: return header + value;
    }
  }
  return 0;
}

uint32_t RtlNtStatusToDosError(uint32_t status) {
  switch (status) {
    case X_STATUS_SUCCESS: return 0;
    case X_STATUS_PENDING: return X_ERROR_IO_PENDING;
    case X_STATUS_TIMEOUT: return 1460;  // ERROR_TIMEOUT
    case X_STATUS_NO_MORE_FILES: return X_ERROR_NO_MORE_FILES;
    case X_STATUS_UNSUCCESSFUL: return 31;  // ERROR_GEN_FAILURE
    case X_STATUS_NOT_IMPLEMENTED: return 1;
    case X_STATUS_INVALID_HANDLE: return X_ERROR_INVALID_HANDLE;
    case X_STATUS_INVALID_PARAMETER: return X_ERROR_INVALID_PARAMETER;
    case X_STATUS_NO_SUCH_FILE:
    case X_STATUS_OBJECT_NAME_NOT_FOUND: return X_ERROR_FILE_NOT_FOUND;
    case X_STATUS_OBJECT_PATH_NOT_FOUND: return X_ERROR_PATH_NOT_FOUND;
    case X_STATUS_END_OF_FILE: return 38;  // ERROR_HANDLE_EOF
    case X_STATUS_NO_MEMORY: return 8;
    case X_STATUS_ACCESS_DENIED: return X_ERROR_ACCESS_DENIED;
    case X_STATUS_BUFFER_TOO_SMALL: return X_ERROR_INSUFFICIENT_BUFFER;
    case X_STATUS_OBJECT_NAME_COLLISION: return X_ERROR_ALREADY_EXISTS;
    case X_STATUS_DEVICE_NOT_CONNECTED: return X_ERROR_DEVICE_NOT_CONNECTED;
    case 0xC000007A:  // STATUS_PROCEDURE_NOT_FOUND
    case 0xC0000263:  // STATUS_DRIVER_ENTRYPOINT_NOT_FOUND
      return 127;     // ERROR_PROC_NOT_FOUND
    case 0xC0000138:  // STATUS_ORDINAL_NOT_FOUND
    case 0xC0000262:  // STATUS_DRIVER_ORDINAL_NOT_FOUND
      return 182;     // ERROR_INVALID_ORDINAL
    default:
      if ((status & 0xFFFF0000) == 0x80070000) return status & 0xFFFF;
      return 317;  // ERROR_MR_MID_NOT_FOUND
  }
}

uint32_t RtlMultiByteToUnicodeN(uint32_t unicode, uint32_t max_bytes, uint32_t written_ptr, uint32_t multibyte,
                                uint32_t multibyte_bytes) {
  uint32_t n = std::min(max_bytes / 2, multibyte_bytes);
  for (uint32_t i = 0; i < n; ++i) Store<uint16_t>(unicode + i * 2, Load<uint8_t>(multibyte + i));
  if (written_ptr) Store<uint32_t>(written_ptr, n * 2);
  return X_STATUS_SUCCESS;
}

uint32_t RtlUnicodeToMultiByteN(uint32_t multibyte, uint32_t max_bytes, uint32_t written_ptr, uint32_t unicode,
                                uint32_t unicode_bytes) {
  uint32_t n = std::min(max_bytes, unicode_bytes / 2);
  for (uint32_t i = 0; i < n; ++i) {
    uint16_t c = Load<uint16_t>(unicode + i * 2);
    Store<uint8_t>(multibyte + i, c < 0x100 ? uint8_t(c) : '?');
  }
  if (written_ptr) Store<uint32_t>(written_ptr, n);
  return X_STATUS_SUCCESS;
}

uint32_t RtlUnicodeStringToAnsiString(uint32_t dest, uint32_t source, uint32_t allocate) {
  uint16_t length = Load<uint16_t>(source) / 2;
  uint32_t src = Load<uint32_t>(source + 4);
  uint32_t buffer;
  if (allocate) {
    buffer = SystemAlloc(length + 1);
    Store<uint16_t>(dest + 2, uint16_t(length + 1));
    Store<uint32_t>(dest + 4, buffer);
  } else {
    buffer = Load<uint32_t>(dest + 4);
    if (Load<uint16_t>(dest + 2) < length + 1) return X_STATUS_BUFFER_OVERFLOW;
  }
  for (uint16_t i = 0; i < length; ++i) {
    uint16_t c = Load<uint16_t>(src + i * 2);
    Store<uint8_t>(buffer + i, c < 0x100 ? uint8_t(c) : '?');
  }
  Store<uint8_t>(buffer + length, 0);
  Store<uint16_t>(dest, length);
  return X_STATUS_SUCCESS;
}

void RtlFreeAnsiString(uint32_t string) {
  // SystemAlloc'd small buffers are not reused; just clear the descriptor.
  Store<uint16_t>(string, 0);
  Store<uint16_t>(string + 2, 0);
  Store<uint32_t>(string + 4, 0);
}

int32_t RtlCompareStringN(uint32_t s1, uint32_t len1, uint32_t s2, uint32_t len2, uint32_t case_insensitive) {
  uint32_t n = std::min(len1, len2);
  for (uint32_t i = 0; i < n; ++i) {
    int c1 = Load<uint8_t>(s1 + i), c2 = Load<uint8_t>(s2 + i);
    if (case_insensitive) {
      c1 = std::toupper(c1);
      c2 = std::toupper(c2);
    }
    if (c1 != c2) return c1 - c2;
  }
  return int32_t(len1) - int32_t(len2);
}

uint32_t RtlUpcaseUnicodeChar(uint32_t c) { return uint16_t(std::towupper(wchar_t(c))); }
uint32_t RtlDowncaseUnicodeChar(uint32_t c) { return uint16_t(std::towlower(wchar_t(c))); }

// RtlCopyString(destination, source): copies as much as fits in the destination's buffer.
void RtlCopyString(x360::hle::ANSI_STRING* destination, const x360::hle::ANSI_STRING* source) {
  if (!source) {
    destination->Length = 0;
    return;
  }
  uint16_t length = std::min<uint16_t>(source->Length, destination->MaximumLength);
  if (length) std::memcpy(x360::hle::to_host(destination->Buffer), x360::hle::to_host(source->Buffer), length);
  destination->Length = length;
}

void HleRtlCaptureContext(uint32_t context) {
  // A zeroed CONTEXT is enough for the callers seen so far (crash reporting paths).
  if (context) std::memset(Ptr(context), 0, 0x400);
}

void HleRtlRaiseException(uint32_t record) {
  uint32_t code = Load<uint32_t>(record);
  if (code == 0x406D1388) {  // MSVC SetThreadName
    uint32_t info = record + 0x14;  // ExceptionInformation[]
    uint32_t name = Load<uint32_t>(info + 4);
    uint32_t id = Load<uint32_t>(info + 8);
    if (name) {
      KLOG_I("thread %X named \"%s\"", id == 0xFFFFFFFF && CurrentThread() ? CurrentThread()->thread_id : id,
             Ptr<const char>(name));
      std::shared_ptr<Thread> named = id == 0xFFFFFFFF ? CurrentThreadShared() : ThreadById(id);
      if (named) named->name = Ptr<const char>(name);
      // The host thread too (debuggers, profilers and the CPU time per thread in the log).
      wchar_t wide[128];
      MultiByteToWideChar(CP_UTF8, 0, Ptr<const char>(name), -1, wide, 128);
      if (id == 0xFFFFFFFF || (CurrentThread() && id == CurrentThread()->thread_id))
        SetThreadDescription(GetCurrentThread(), wide);
      else if (named && named->host && wide[0])
        SetThreadDescription(static_cast<HANDLE>(named->host), wide);
    }
    return;
  }
  KLOG_W("RtlRaiseException %08X (ignored)", code);
}

void HleRtlUnwind(uint32_t target_frame, uint32_t target_ip, uint32_t record, uint32_t return_value) {
  KLOG_W("RtlUnwind(%08X, %08X, %08X, %08X) is not supported", target_frame, target_ip, record, return_value);
}

uint32_t C_specific_handler() { return 1; }  // ExceptionContinueSearch

}  // namespace

// ---- variadic exports (written by hand: they read their own arguments) ---------------------------

PPC_FUNC(__imp__DbgPrint) {
  (void)base;
  LogGuest(GuestFormat(ctx.r3.u32, ArgSource{&ctx, 1}));
  ctx.r3.u64 = X_STATUS_SUCCESS;
}

PPC_FUNC(__imp__sprintf) {
  (void)base;
  std::string s = GuestFormat(ctx.r4.u32, ArgSource{&ctx, 2});
  ctx.r3.s64 = StoreFormatted(ctx.r3.u32, -1, s);
}

PPC_FUNC(__imp___snprintf) {
  (void)base;
  std::string s = GuestFormat(ctx.r5.u32, ArgSource{&ctx, 3});
  ctx.r3.s64 = StoreFormatted(ctx.r3.u32, int64_t(ctx.r4.u32), s);
}

PPC_FUNC(__imp___vsnprintf) {
  (void)base;
  ArgSource args;
  args.va_list_ptr = ctx.r6.u32;
  std::string s = GuestFormat(ctx.r5.u32, args);
  ctx.r3.s64 = StoreFormatted(ctx.r3.u32, int64_t(ctx.r4.u32), s);
}

// vswprintf(buffer, format, va_list): the kernel's has no buffer size (the old MSVC form). Returns the
// number of characters written, without the terminator.
PPC_FUNC(__imp__vswprintf) {
  (void)base;
  ArgSource args;
  args.va_list_ptr = ctx.r5.u32;
  std::u16string s = GuestFormatWide(ctx.r4.u32, args);
  uint32_t buffer = ctx.r3.u32;
  for (size_t i = 0; i < s.size(); ++i) Store<uint16_t>(buffer + uint32_t(i) * 2, uint16_t(s[i]));
  Store<uint16_t>(buffer + uint32_t(s.size()) * 2, 0);
  ctx.r3.s64 = int32_t(s.size());
}

X360_EXPORT(RtlImageXexHeaderField, RtlImageXexHeaderField)
X360_EXPORT(RtlNtStatusToDosError, RtlNtStatusToDosError)
X360_EXPORT(RtlMultiByteToUnicodeN, RtlMultiByteToUnicodeN)
X360_EXPORT(RtlUnicodeToMultiByteN, RtlUnicodeToMultiByteN)
X360_EXPORT(RtlUnicodeStringToAnsiString, RtlUnicodeStringToAnsiString)
X360_EXPORT(RtlFreeAnsiString, RtlFreeAnsiString)
X360_EXPORT(RtlCompareStringN, RtlCompareStringN)
X360_EXPORT(RtlUpcaseUnicodeChar, RtlUpcaseUnicodeChar)
X360_EXPORT(RtlDowncaseUnicodeChar, RtlDowncaseUnicodeChar)
X360_EXPORT(RtlCopyString, RtlCopyString)
X360_EXPORT(RtlCaptureContext, HleRtlCaptureContext)
X360_EXPORT(RtlRaiseException, HleRtlRaiseException)
X360_EXPORT(RtlUnwind, HleRtlUnwind)
X360_EXPORT(__C_specific_handler, C_specific_handler)
