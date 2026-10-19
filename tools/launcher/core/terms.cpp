#include "core/terms.h"

#include <cstdint>
#include <initializer_list>

#include "core/paths.h"
#include "launcher_terms.h"

namespace anyx360::launcher {

std::wstring TermsText(Language language) {
  std::wstring text = Widen(language == Language::kSpanish ? kTermsEsUtf8 : kTermsEnUtf8);
  std::erase(text, L'\r');  // the generated header may have \r\n
  return text;
}

std::wstring TermsHash() {
  uint64_t hash = 14695981039346656037ull;
  for (const char* text : {kTermsEnUtf8, kTermsEsUtf8})
    for (const char* c = text; *c; ++c) hash = (hash ^ uint8_t(*c)) * 1099511628211ull;
  wchar_t text[20];
  swprintf(text, 20, L"%016llx", static_cast<unsigned long long>(hash));
  return text;
}

bool TermsAccepted() { return ReadSetting(SettingsFile(L"terms_accepted.txt")) == TermsHash(); }

void AcceptTerms() { WriteSetting(SettingsFile(L"terms_accepted.txt"), TermsHash()); }

}  // namespace anyx360::launcher
