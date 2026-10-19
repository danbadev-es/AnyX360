// The terms of use (tools/launcher/terms_en.txt and terms_es.txt naming every game, compiled in):
// accepted before the first use, and again whenever either text changes
// (%LOCALAPPDATA%\AnyX360\terms_accepted.txt holds a hash of both).
#pragma once

#include <string>

#include "core/strings.h"

namespace anyx360::launcher {

// The text in `language`, lines ending in \n.
std::wstring TermsText(Language language);
// FNV-1a of both texts: a new version of either must be accepted again.
std::wstring TermsHash();
bool TermsAccepted();
void AcceptTerms();

}  // namespace anyx360::launcher
