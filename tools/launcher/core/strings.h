// The launcher's text, in English and Spanish, and the messages a job ends with.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CatalogGame;

namespace anyx360::launcher {

enum class Language { kEnglish, kSpanish };

Language CurrentLanguage();
void SetLanguage(Language language);
// The saved choice (%LOCALAPPDATA%\AnyX360\launcher_language.txt), else Windows' display language.
void LoadLanguage();
// Sets the language and remembers it.
void SaveLanguage(Language language);

enum Text {
  kTextChecking,
  kTextFinishing,
  kTextExtracting,  // {0} game, {1} percent
  kTextRemoving,    // {0} game
  kTextPlay,
  kTextAddTile,
  kTextCancel,
  kTextMenuCover,
  kTextMenuRestoreCover,
  kTextMenuLogs,
  kTextMenuPrepareAgain,
  kTextMenuRemove,
  kTextDialogTitle,
  kTextFolderTitle,
  kTextCoverTitle,  // {0} game
  kTextFilterArchive,
  kTextFilterImages,
  kTextFilterAll,
  kTextMissing7Zip,
  kTextConfirmReplace,  // {0} game
  kTextConfirmRemove,   // {0} game
  kTextConfirmCancelExit,
  kTextAdded,        // {0} game
  kTextAddedFolder,  // {0} game, {1} folder
  kTextRemoved,      // {0} game
  kTextCancelled,
  kTextErrorList,         // {0} 7-Zip exit code (the file isn't an archive 7-Zip opens)
  kTextErrorNotGame,
  kTextErrorNoXexFolder,
  kTextErrorUnsupported,  // {0} title ID found, {1} supported games
  kTextErrorMissingFile,  // {0} file
  kTextErrorLocationGone,  // {0} game, {1} folder
  kTextErrorSpace,        // {0} needed, {1} free
  kTextErrorExtract,      // {0} 7-Zip exit code
  kTextErrorNoXex,
  kTextErrorMove,         // {0} reason
  kTextErrorNoExe,        // {0} executable
  kTextErrorStart,        // {0} Windows error
  kTextErrorExited,       // {0} game, {1} exit code
  kTextErrorCover,
  kTextAddedContent,  // {0} game, {1} number of downloadable content packs
  kTextErrorConvert,  // {0} reason
  kTextTermsHeading,
  kTextTermsCheck,
  kTextTermsAccept,
  kTextTermsDecline,
  kTextTermsClose,
  kTextAskPassword,        // {0} archive
  kTextErrorPassword,
  kTextErrorWrongPassword,
  kTextErrorMissingVolume,  // {0} archive
  kTextErrorUnknownFormat,
  kTextErrorContentOnly,   // {0} game
  kTextErrorUpdateOnly,
  kTextErrorContentNoGame,  // {0} game
  kTextAddedContentOnly,   // {0} game, {1} number of packs
  kTextNoteUpdatesSkipped,  // {0} number of title updates
  kTextNoteOtherContent,   // {0} number of other titles' packages
  kTextNoteContentSkipped,  // {0} number of downloadable content packs
  kTextMyGames,
  kTextCompatibleTitle,
  kTextCompatibleCount,  // {0} playable, {1} coming soon
  kTextStatusPlayable,
  kTextStatusTesting,
  kTextStatusInLibrary,
  kTextStatusComing,
  kTextAddFirst,
  kTextAddFirstHint,  // {0} games this release plays
  kTextAddTitle,
  kTextFromFile,
  kTextFromFileHint,
  kTextFromFolder,
  kTextFromFolderHint,
  kTextDropHint,
  kTextDropHere,
  kTextAddingTitle,  // {0} game
  kTextCheckingTitle,
  kTextRemovingTitle,  // {0} game
  kTextPasswordTitle,
  kTextPasswordKeyboard,
  kTextOk,
  kTextReplace,
  kTextKeep,
  kTextRemove,
  kTextExit,
  kTextKeepGoing,
  kTextOpenLogs,
  kTextProblemTitle,
  kTextGameClosedTitle,
  kTextReplaceTitle,
  kTextRemoveTitle,
  kTextExitTitle,
  kTextOtherLanguage,
  kTextTermsLink,
  kTextDeveloped,
  kTextHintSelect,
  kTextHintBack,
  kTextHintMore,
  kTextHintCompatible,
  kTextLocationLine,  // {0} folder
  kTextExtractedLine,
  kTextKeyboardPad,
  kTextWebcam,
  kTextProfileLink,      // the header's link when player 1 has no name
  kTextProfileTitle,
  kTextProfileText,
  kTextPlayerName,       // {0} 1-4
  kTextNameRules,
  kTextGameLanguage,
  kTextLanguageAuto,     // {0} Windows' language, as the console names it
  kTextLanguageNoAuto,   // Windows' language isn't one of the console's
  kTextSave,
  kTextMenuSaveAnyx,
  kTextSaveAnyxTitle,      // {0} game
  kTextFilterAnyx,
  kTextSaving,             // {0} game, {1} percent
  kTextSavingTitle,        // {0} game
  kTextVerifying,          // {0} game, {1} percent
  kTextSaved,              // {0} game, {1} file, {2} size
  kTextErrorVersion,       // {0} game, {1} the copy's version, {2} the one AnyX360 plays
  kTextErrorAnyxNewer,     // {0} the AnyX360 that wrote it
  kTextErrorAnyxDamaged,   // {0} file
  kTextErrorSaveHere,
  kTextErrorSaveFailed,    // {0} 7-Zip exit code
  kTextErrorSaveLink,      // {0} folder
  kTextErrorSpaceOn,       // {0} needed, {1} free, {2} drive
  kTextErrorFat32,         // {0} drive
  kTextErrorNotInLibrary,  // {0} game
  kTextCount
};

// `text` with {0}, {1}... replaced.
std::wstring Format(std::wstring text, const std::vector<std::wstring>& args);
// A text in the current language with {0}, {1}... replaced.
std::wstring Tr(Text id, const std::vector<std::wstring>& args = {});

// The outcome of an action, kept untranslated so it follows language changes.
struct Message {
  Text id = kTextCount;  // none
  std::vector<std::wstring> args;
  bool error = false;
  bool empty() const { return id == kTextCount; }
  std::wstring Render() const { return empty() ? std::wstring() : Tr(id, args); }
};

// "a", "a and b", "a, b and c" in the current language.
std::wstring JoinTitles(const std::vector<const CatalogGame*>& games);
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& text);  // to UTF-8
// A path short enough for one line of a message ("C:\Users\...\Disc\Game").
std::wstring ShortPath(const std::filesystem::path& path);
std::wstring Gigabytes(uint64_t bytes);
std::wstring Hex32(uint32_t value);

}  // namespace anyx360::launcher
