// Windows launcher for a distribution folder: prepares the game files from the player's disc
// archive (.7z) or from a folder where their copy already is, and starts the recompiled game,
// logging to logs\.
//
//   <launcher>.exe           this program
//   bin\<game>.exe (+ .pdb)  the recompiled game (the .pdb names functions in crash reports)
//   bin\7z.exe, bin\7z.dll   7-Zip, to extract the archive
//   bin\shader_cache\        Xenia shader storage, copied once to the player's cache
//   keys\xex2_retail.key
//   game\                    the disc folder holding default.xex, extracted here by the launcher
//   game_location.txt        or the folder of the player's copy, used where it is (UTF-8)
//   logs\                    last_run.log (the game's output) and previous_run.log
//
// Either way the copy must be the right game: default.xex's title ID and the game's required files
// are checked. Run from the build folder (game executable next to the launcher, no bin\) it works
// the same. `--archive <file.7z>` and `--folder <dir>` start preparing without asking (unattended
// tests).
//
// The text is in English and Spanish: Windows' display language picks one, the footer switches
// (remembered in %LOCALAPPDATA%\AnyX360\launcher_language.txt). The terms of use
// (tools/launcher/terms_en.txt and terms_es.txt with the game's details, compiled in) must be accepted
// before the first use of each game, and again whenever either text changes
// (%LOCALAPPDATA%\AnyX360\<game>\terms_accepted.txt holds a hash).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "anyx360_version.h"
#include ANYX360_TERMS_HEADER

namespace fs = std::filesystem;

#define ANYX360_WIDEN2(x) L##x
#define ANYX360_WIDEN(x) ANYX360_WIDEN2(x)
#define ANYX360_STRINGIFY2(x) #x
#define ANYX360_STRINGIFY(x) ANYX360_STRINGIFY2(x)

namespace {

const wchar_t* const kGameExe = ANYX360_WIDEN(ANYX360_GAME_EXE);
const wchar_t* const kGameTitle = ANYX360_WIDEN(ANYX360_GAME_TITLE);
const wchar_t* const kUserDir = ANYX360_WIDEN(ANYX360_USER_DIR);
const wchar_t* const kVersion = ANYX360_WIDEN(ANYX360_VERSION_FULL);
const wchar_t* const kGameSize = ANYX360_WIDEN(ANYX360_STRINGIFY(ANYX360_GAME_SIZE_GB));
constexpr bool kWebcam = ANYX360_GAME_WEBCAM != 0;  // played with a webcam (virtual Kinect)
constexpr uint32_t kTitleId = ANYX360_GAME_TITLE_ID;
// Files a complete copy has besides default.xex ('|'-separated, relative to its folder).
const wchar_t* const kRequiredFiles = ANYX360_WIDEN(ANYX360_GAME_REQUIRED_FILES);

constexpr UINT kMsgProgress = WM_APP + 1;    // wParam: percent (-1 while checking the archive)
constexpr UINT kMsgPrepared = WM_APP + 2;    // lParam: Message* (an error), or null on success
constexpr UINT kMsgGameExited = WM_APP + 3;  // wParam: exit code

enum Control { kTitle = 100, kSubtitle, kStatus, kProgress, kPlay, kPrepare, kLogs, kMessage, kFooter };
enum TermsControl { kTermsHeading = 200, kTermsText, kTermsCheck, kTermsAccept, kTermsDecline };
enum class State { kIdle, kPreparing, kRunning };

// ---- text -------------------------------------------------------------------------------------

enum class Language { kEnglish, kSpanish };
Language g_language = Language::kEnglish;

enum Text {
  kTextSubtitle,  // {0} version
  kTextChecking,
  kTextFinishing,
  kTextExtracting,  // {0} percent
  kTextReadyPad,
  kTextReadyWebcam,
  kTextMissing,  // {0} game, {1} size in GB
  kTextPlay,
  kTextChoose,
  kTextPrepareAgain,
  kTextCancel,
  kTextLogs,
  kTextFooter,
  kTextMenuArchive,
  kTextMenuFolder,
  kTextDialogTitle,  // {0} game
  kTextFolderTitle,  // {0} game
  kTextFilter7z,
  kTextFilterAll,
  kTextMissing7Zip,
  kTextConfirmPrepareAgain,
  kTextConfirmCancelExit,
  kTextPrepared,
  kTextPreparedFolder,  // {0} folder
  kTextCancelled,
  kTextErrorList,       // {0} 7-Zip exit code
  kTextErrorNotGame,    // {0} game
  kTextErrorNoXexFolder,  // {0} game
  kTextErrorWrongTitle,   // {0} game, {1} title ID found
  kTextErrorMissingFile,  // {0} file
  kTextErrorLocationGone,  // {0} folder
  kTextErrorSpace,      // {0} needed, {1} free
  kTextErrorExtract,    // {0} 7-Zip exit code
  kTextErrorNoXex,
  kTextErrorMove,       // {0} reason
  kTextErrorNoExe,      // {0} executable
  kTextErrorStart,      // {0} Windows error
  kTextErrorExited,     // {0} exit code
  kTextTermsTitle,      // {0} game
  kTextTermsHeading,
  kTextTermsCheck,
  kTextTermsAccept,
  kTextTermsDecline,
  kTextTermsClose,
  kTextCount
};

const wchar_t* const kEnglish[kTextCount] = {
    L"AnyX360 {0} · native PC recompilation",
    L"Checking the game…",
    L"Finishing…",
    L"Extracting the game… {0} %",
    L"Ready to play. Keyboard and mouse or an Xbox controller; F2 opens the settings in game.",
    L"Ready to play. Stand 2-3 m from your webcam with your whole body in view; F2 opens the settings in "
    L"game.",
    L"First the game has to be prepared: choose the .7z archive with your {0} disc (it is extracted into "
    L"this folder, about {1} GB) or the folder where your copy already is.",
    L"Play",
    L"Prepare the game…",
    L"Prepare again…",
    L"Cancel",
    L"Logs",
    L"Developed by danbadev-es  ·  <a>Terms of use</a>  ·  <a>Español</a>",
    L"From a .7z archive…",
    L"From a game folder…",
    L"Choose the .7z archive of {0}",
    L"Choose the folder of {0} (the one with default.xex)",
    L"7z archive",
    L"All files",
    L"7-Zip is missing (bin\\7z.exe). Extract the whole folder again.",
    L"The game is already prepared. Prepare it again? The current setup will be replaced.",
    L"Cancel the extraction and exit?",
    L"The game is ready.",
    L"The game is ready. It is played from {0}: don't move or delete that folder.",
    L"Extraction cancelled.",
    L"7-Zip could not read the archive (code {0}). Check that it is the game's .7z and that it is complete.",
    L"That archive has no default.xex: it doesn't look like the {0} disc.",
    L"That folder has no default.xex: choose the folder of your {0} copy.",
    L"That copy is another game (title {1}), not {0}.",
    L"The copy is incomplete: {0} is missing.",
    L"The game folder is no longer available ({0}). Prepare the game again.",
    L"Not enough space on this drive: {0} needed, {1} free. Move this folder to another drive or free up "
    L"space.",
    L"The extraction failed (7-Zip code {0}). Check the free space and that the archive isn't damaged.",
    L"default.xex was not found after extracting.",
    L"The game could not be moved to the game folder ({0}).",
    L"{0} was not found in the bin folder.",
    L"The game could not be started (error {0}).",
    L"The game closed with an error (code {0}). Press Logs and send the last_run.log file to whoever gave "
    L"you the game.",
    L"Terms of use - {0} (AnyX360)",
    L"Terms of use",
    L"I have read and accept the terms of use",
    L"Accept",
    L"Decline",
    L"Close",
};

const wchar_t* const kSpanish[kTextCount] = {
    L"AnyX360 {0} · recompilación nativa para PC",
    L"Comprobando el juego…",
    L"Terminando…",
    L"Extrayendo el juego… {0} %",
    L"Listo para jugar. Teclado y ratón o mando de Xbox; F2 abre los ajustes durante la partida.",
    L"Listo para jugar. Ponte a 2-3 m de la webcam, con todo el cuerpo a la vista; F2 abre los ajustes "
    L"durante la partida.",
    L"Primero hay que preparar el juego: elige el archivo .7z con tu disco de {0} (se extrae en esta "
    L"carpeta, unos {1} GB) o la carpeta donde ya tienes tu copia.",
    L"Jugar",
    L"Preparar el juego…",
    L"Volver a preparar…",
    L"Cancelar",
    L"Registros",
    L"Desarrollado por danbadev-es  ·  <a>Términos y condiciones de uso</a>  ·  <a>English</a>",
    L"Desde un archivo .7z…",
    L"Desde una carpeta del juego…",
    L"Elige el archivo .7z de {0}",
    L"Elige la carpeta de {0} (la que tiene default.xex)",
    L"Archivo 7z",
    L"Todos los archivos",
    L"Falta 7-Zip (bin\\7z.exe). Vuelve a descomprimir la carpeta completa.",
    L"El juego ya está preparado. ¿Prepararlo otra vez? Se reemplazará la preparación actual.",
    L"¿Cancelar la extracción y salir?",
    L"El juego está preparado.",
    L"El juego está preparado. Se juega desde {0}: no muevas ni borres esa carpeta.",
    L"Extracción cancelada.",
    L"7-Zip no pudo leer el archivo (código {0}). Comprueba que es el .7z del juego y que está completo.",
    L"Ese archivo no contiene default.xex: no parece el disco de {0}.",
    L"Esa carpeta no contiene default.xex: elige la carpeta de tu copia de {0}.",
    L"Esa copia es de otro juego (título {1}), no de {0}.",
    L"La copia está incompleta: falta {0}.",
    L"La carpeta del juego ya no está disponible ({0}). Vuelve a preparar el juego.",
    L"No hay espacio suficiente en esta unidad: hacen falta {0} y hay {1}. Mueve esta carpeta a otro disco "
    L"o libera espacio.",
    L"La extracción falló (código de 7-Zip {0}). Comprueba el espacio libre y que el archivo no esté dañado.",
    L"Después de extraer no se encontró default.xex.",
    L"No se pudo mover el juego a la carpeta game ({0}).",
    L"No se encuentra {0} en la carpeta bin.",
    L"No se pudo iniciar el juego (error {0}).",
    L"El juego se cerró con un error (código {0}). Pulsa Registros y envía el archivo last_run.log a quien "
    L"te pasó el juego.",
    L"Términos y condiciones de uso - {0} (AnyX360)",
    L"Términos y condiciones de uso",
    L"He leído y acepto los términos y condiciones de uso",
    L"Aceptar",
    L"No acepto",
    L"Cerrar",
};

// A text in the current language with {0}, {1}... replaced.
std::wstring Tr(Text id, std::initializer_list<std::wstring> args = {}) {
  std::wstring text = (g_language == Language::kSpanish ? kSpanish : kEnglish)[id];
  size_t index = 0;
  for (const std::wstring& arg : args) {
    std::wstring marker = L"{" + std::to_wstring(index++) + L"}";
    for (size_t at; (at = text.find(marker)) != std::wstring::npos;) text.replace(at, marker.size(), arg);
  }
  return text;
}

// The outcome of the last action, kept untranslated so it follows language changes.
struct Message {
  Text id = kTextCount;  // none
  std::vector<std::wstring> args;
  bool error = false;
  std::wstring Render() const {
    if (id == kTextCount) return {};
    std::wstring text = Tr(id);
    for (size_t i = 0; i < args.size(); ++i) {
      std::wstring marker = L"{" + std::to_wstring(i) + L"}";
      for (size_t at; (at = text.find(marker)) != std::wstring::npos;) text.replace(at, marker.size(), args[i]);
    }
    return text;
  }
};

HWND g_window = nullptr;
HFONT g_title_font = nullptr, g_text_font = nullptr, g_small_font = nullptr;
State g_state = State::kIdle;
Message g_message;  // shown above the status
int g_percent = -1;
std::thread g_worker;
std::atomic<bool> g_cancel{false};
bool g_close_after_cancel = false;
std::mutex g_extractor_mutex;
HANDLE g_extractor = nullptr;  // the running 7-Zip process

// ---- paths ------------------------------------------------------------------------------------

fs::path Root() {
  static const fs::path root = [] {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
  }();
  return root;
}

fs::path ExtractedDir() { return Root() / L"game"; }
fs::path LocationFile() { return Root() / L"game_location.txt"; }
fs::path LogDir() { return Root() / L"logs"; }

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

// A one-line UTF-8 text file (paths may have any characters).
std::wstring ReadUtf8(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);  // saved by an editor with a BOM
  while (!bytes.empty() && (bytes.back() == '\n' || bytes.back() == '\r')) bytes.pop_back();
  int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), nullptr, 0);
  std::wstring text(size_t(std::max(length, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.data(), int(bytes.size()), text.data(), length);
  return text;
}

bool WriteUtf8(const fs::path& path, const std::wstring& text) {
  int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string bytes(size_t(std::max(length, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), length, nullptr, nullptr);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << bytes << "\n";
  return bool(out);
}

// The folder of a copy prepared in place, or empty.
fs::path StoredLocation() { return Exists(LocationFile()) ? fs::path(ReadUtf8(LocationFile())) : fs::path(); }

// The prepared copy: the game\ folder extracted from an archive, else the player's own folder.
fs::path GameDir() {
  if (Exists(ExtractedDir() / L"default.xex")) return ExtractedDir();
  fs::path stored = StoredLocation();
  return stored.empty() ? ExtractedDir() : stored;
}

bool GameReady() { return Exists(GameDir() / L"default.xex"); }

// bin\ in a distribution folder, next to the launcher in the build folder.
fs::path FindProgram(const wchar_t* name) {
  for (const fs::path& path : {Root() / L"bin" / name, Root() / name})
    if (Exists(path)) return path;
  return {};
}

fs::path SevenZip() {
  fs::path bundled = FindProgram(L"7z.exe");
  if (!bundled.empty()) return bundled;
  wchar_t* program_files = nullptr;
  fs::path installed;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &program_files)))
    installed = fs::path(program_files) / L"7-Zip" / L"7z.exe";
  CoTaskMemFree(program_files);
  return Exists(installed) ? installed : fs::path();
}

// %LOCALAPPDATA%, as the game resolves it.
fs::path LocalAppData() {
  wchar_t path[MAX_PATH];
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
  if (length && length < MAX_PATH) return path;
  wchar_t* known = nullptr;
  fs::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) result = known;
  CoTaskMemFree(known);
  return result;
}

// Launcher settings shared by every game, and the game's own (next to its saves and settings).
fs::path SettingsFile(const wchar_t* name) { return LocalAppData() / L"AnyX360" / name; }
fs::path GameSettingsFile(const wchar_t* name) { return LocalAppData() / L"AnyX360" / kUserDir / name; }

std::wstring ReadSetting(const fs::path& path) {
  std::wifstream in(path);
  std::wstring value;
  std::getline(in, value);
  return value;
}

void WriteSetting(const fs::path& path, const std::wstring& value) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::wofstream(path) << value << L"\n";
}

std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }

// A path short enough for one line of a message ("C:\Users\...\Disc\Game").
std::wstring ShortPath(const fs::path& path) {
  wchar_t shortened[MAX_PATH];
  return PathCompactPathExW(shortened, path.c_str(), 56, 0) ? shortened : path.wstring();
}

std::wstring Gigabytes(uint64_t bytes) {
  wchar_t text[32];
  swprintf(text, 32, L"%.1f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

void LoadLanguage() {
  std::wstring saved = ReadSetting(SettingsFile(L"launcher_language.txt"));
  if (saved == L"es" || saved == L"en")
    g_language = saved == L"es" ? Language::kSpanish : Language::kEnglish;
  else
    g_language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH ? Language::kSpanish : Language::kEnglish;
}

// ---- 7-Zip ------------------------------------------------------------------------------------

// Runs a console program without a window and passes its output (stdout and stderr) to `output`
// as it arrives. Returns the exit code, or -1 if the program could not start.
int RunCaptured(std::wstring command, const std::function<void(const std::string&)>& output,
                bool track_as_extractor) {
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &inherit, 0)) return -1;
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr);
  STARTUPINFOW startup{sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input;
  startup.hStdOutput = write;
  startup.hStdError = write;
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                &startup, &process);
  CloseHandle(write);
  CloseHandle(input);
  if (!started) {
    CloseHandle(read);
    return -1;
  }
  if (track_as_extractor) {
    std::lock_guard lock(g_extractor_mutex);
    g_extractor = process.hProcess;
  }
  char buffer[4096];
  DWORD length = 0;
  while (ReadFile(read, buffer, sizeof(buffer), &length, nullptr) && length) output(std::string(buffer, length));
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  if (track_as_extractor) {
    std::lock_guard lock(g_extractor_mutex);
    g_extractor = nullptr;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(read);
  return int(code);
}

struct ArchiveInfo {
  uint64_t size = 0;      // uncompressed
  bool has_game = false;  // contains default.xex
};

bool ListArchive(const fs::path& archive, ArchiveInfo& info, Message& error) {
  std::string output;
  int code = RunCaptured(Quote(SevenZip()) + L" l -sccUTF-8 " + Quote(archive),
                         [&](const std::string& text) { output += text; }, false);
  if (code != 0) {
    error = {kTextErrorList, {std::to_wstring(code)}, true};
    return false;
  }
  std::vector<std::string> lines;
  for (size_t start = 0; start < output.size();) {
    size_t end = output.find('\n', start);
    if (end == std::string::npos) end = output.size();
    std::string line = output.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
    start = end + 1;
  }
  size_t summary = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string lower = lines[i];
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower.size() >= 11 && lower.compare(lower.size() - 11, 11, "default.xex") == 0) info.has_game = true;
    if (lines[i].rfind("-------------------", 0) == 0) summary = i + 1;
  }
  // Totals line after the last separator: "<date> <time> <size> <compressed> <n> files, ...".
  if (summary && summary < lines.size()) {
    std::vector<std::string> tokens;
    for (size_t start = 0; start < lines[summary].size();) {
      size_t begin = lines[summary].find_first_not_of(' ', start);
      if (begin == std::string::npos) break;
      size_t end = lines[summary].find(' ', begin);
      if (end == std::string::npos) end = lines[summary].size();
      tokens.push_back(lines[summary].substr(begin, end - begin));
      start = end;
    }
    if (tokens.size() > 2 && !tokens[2].empty() &&
        std::all_of(tokens[2].begin(), tokens[2].end(), [](char c) { return c >= '0' && c <= '9'; }))
      info.size = std::stoull(tokens[2]);
  }
  if (!info.size) {
    std::error_code ec;
    info.size = fs::file_size(archive, ec);
  }
  return true;
}

void RemoveAll(const fs::path& path) {
  std::error_code ec;
  fs::remove_all(path, ec);
}

// The folder holding default.xex: `folder` itself or one up to 4 levels below it (archives and copies
// often have the disc inside a folder or two).
fs::path FindGameRoot(const fs::path& folder) {
  if (Exists(folder / L"default.xex")) return folder;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() > 4) {
      it.disable_recursion_pending();
      continue;
    }
    std::wstring name = it->path().filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    if (name == L"default.xex" && it->is_regular_file(ec)) return it->path().parent_path();
  }
  return {};
}

// The title ID in default.xex's header: the execution info (optional header 0x00040006) holds it at
// offset 0xC. All big-endian.
bool ReadTitleId(const fs::path& xex, uint32_t& title_id) {
  std::ifstream in(xex, std::ios::binary);
  std::vector<uint8_t> header(0x10000);
  in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
  size_t size = size_t(in.gcount());
  auto be32 = [&](size_t at) -> uint32_t {
    if (at + 4 > size) return 0;
    return uint32_t(header[at]) << 24 | uint32_t(header[at + 1]) << 16 | uint32_t(header[at + 2]) << 8 | header[at + 3];
  };
  if (be32(0) != 0x58455832) return false;  // 'XEX2'
  uint32_t count = std::min<uint32_t>(be32(0x14), 256);
  for (uint32_t i = 0; i < count; ++i) {
    if (be32(0x18 + i * 8) != 0x00040006) continue;
    uint32_t info = be32(0x1C + i * 8);
    if (size_t(info) + 0x10 > size) return false;
    title_id = be32(info + 0xC);
    return true;
  }
  return false;
}

// A copy must be this game (the recompiled code only runs its own executable) and complete.
bool CheckCopy(const fs::path& root, Message& error) {
  uint32_t title_id = 0;
  if (!ReadTitleId(root / L"default.xex", title_id) || title_id != kTitleId) {
    wchar_t found[16];
    swprintf(found, 16, L"%08X", title_id);
    error = {kTextErrorWrongTitle, {kGameTitle, found}, true};
    return false;
  }
  std::wstring files = kRequiredFiles;
  for (size_t start = 0; start < files.size();) {
    size_t end = files.find(L'|', start);
    if (end == std::wstring::npos) end = files.size();
    std::wstring file = files.substr(start, end - start);
    start = end + 1;
    if (!file.empty() && !Exists(root / file)) {
      error = {kTextErrorMissingFile, {file}, true};
      return false;
    }
  }
  return true;
}

// Removes an earlier extraction (game\), which would take precedence over a copy used in place.
void RemoveExtracted() {
  if (!Exists(ExtractedDir())) return;
  std::error_code ec;
  fs::path old = Root() / L"game.old";
  RemoveAll(old);
  fs::rename(ExtractedDir(), old, ec);
  RemoveAll(old);
}

// Worker thread: checks the player's folder and remembers it; the copy is played where it is.
void PrepareFolderWorker(fs::path folder) {
  auto finish = [](const Message& result) { PostMessageW(g_window, kMsgPrepared, 0, LPARAM(new Message(result))); };
  fs::path root = FindGameRoot(folder);
  if (root.empty()) return finish({kTextErrorNoXexFolder, {kGameTitle}, true});
  Message error;
  if (!CheckCopy(root, error)) return finish(error);
  std::error_code ec;
  root = fs::absolute(root, ec);
  fs::path extracted = fs::absolute(ExtractedDir(), ec);
  auto relative = root.lexically_relative(extracted);
  if (!relative.empty() && *relative.begin() != L"..") {  // the extracted copy itself
    fs::remove(LocationFile(), ec);
    return finish({kTextPrepared, {}, false});
  }
  RemoveExtracted();
  if (!WriteUtf8(LocationFile(), root.wstring())) return finish({kTextErrorMove, {L"game_location.txt"}, true});
  finish({kTextPreparedFolder, {ShortPath(root)}, false});
}

// Worker thread: checks and extracts the archive, then moves the folder holding default.xex to
// game\. Posts progress and, at the end, kMsgPrepared.
void PrepareWorker(fs::path archive) {
  auto finish = [](const Message& error) {
    PostMessageW(g_window, kMsgPrepared, 0, error.id == kTextCount ? 0 : LPARAM(new Message(error)));
  };
  ArchiveInfo info;
  Message error;
  if (!ListArchive(archive, info, error)) return finish(error);
  if (!info.has_game) return finish({kTextErrorNotGame, {kGameTitle}, true});
  ULARGE_INTEGER free_bytes{};
  uint64_t needed = info.size + (512ull << 20);
  if (GetDiskFreeSpaceExW(Root().c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < needed)
    return finish({kTextErrorSpace, {Gigabytes(needed), Gigabytes(free_bytes.QuadPart)}, true});

  fs::path partial = Root() / L"game.partial";
  RemoveAll(partial);  // a previous attempt
  PostMessageW(g_window, kMsgProgress, 0, 0);
  int last = 0;
  std::string tail;
  int code = RunCaptured(
      Quote(SevenZip()) + L" x -y -bsp1 -bso0 -bse1 -sccUTF-8 -o" + Quote(partial) + L" " + Quote(archive),
      [&](const std::string& text) {
        // Progress lines ("  42% 120 - <file>") overwrite each other with backspaces.
        std::string chunk = tail + text;
        int percent = -1;
        for (size_t i = 0; i < chunk.size(); ++i) {
          if (chunk[i] != '%') continue;
          size_t begin = i;
          while (begin > 0 && i - begin < 3 && std::isdigit(static_cast<unsigned char>(chunk[begin - 1]))) --begin;
          if (begin < i) percent = std::stoi(chunk.substr(begin, i - begin));
        }
        tail = chunk.size() > 8 ? chunk.substr(chunk.size() - 8) : chunk;
        if (percent >= 0 && percent != last) {
          last = percent;
          PostMessageW(g_window, kMsgProgress, WPARAM(std::min(percent, 99)), 0);
        }
      },
      true);
  if (g_cancel) {
    RemoveAll(partial);
    return finish({});
  }
  if (code != 0) {
    RemoveAll(partial);
    return finish({kTextErrorExtract, {std::to_wstring(code)}, true});
  }
  PostMessageW(g_window, kMsgProgress, 100, 0);

  fs::path found = FindGameRoot(partial);
  if (found.empty()) {
    RemoveAll(partial);
    return finish({kTextErrorNoXex, {}, true});
  }
  if (!CheckCopy(found, error)) {
    RemoveAll(partial);
    return finish(error);
  }
  RemoveExtracted();  // preparing again: replace the old files
  std::error_code ec;
  fs::rename(found, ExtractedDir(), ec);
  if (ec) {
    std::string reason = ec.message();
    RemoveAll(partial);
    return finish({kTextErrorMove, {std::wstring(reason.begin(), reason.end())}, true});
  }
  if (found != partial) RemoveAll(partial);
  fs::remove(LocationFile(), ec);  // a copy used in place before
  finish({});
}

// ---- terms of use -----------------------------------------------------------------------------

std::wstring TermsText() {
  const char* utf8 = g_language == Language::kSpanish ? kTermsEsUtf8 : kTermsEnUtf8;
  int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  std::wstring text(size_t(std::max(length, 1)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, text.data(), length);
  text.resize(wcslen(text.c_str()));
  std::wstring crlf;  // the edit control needs \r\n (the generated header may already have \r\n)
  for (wchar_t c : text) {
    if (c == L'\r') continue;
    if (c == L'\n') crlf += L'\r';
    crlf += c;
  }
  return crlf;
}

// FNV-1a of both texts: a new version of either must be accepted again.
std::wstring TermsHash() {
  uint64_t hash = 14695981039346656037ull;
  for (const char* text : {kTermsEnUtf8, kTermsEsUtf8})
    for (const char* c = text; *c; ++c) hash = (hash ^ uint8_t(*c)) * 1099511628211ull;
  wchar_t text[20];
  swprintf(text, 20, L"%016llx", static_cast<unsigned long long>(hash));
  return text;
}

bool TermsAccepted() { return ReadSetting(GameSettingsFile(L"terms_accepted.txt")) == TermsHash(); }

// ---- running the game -------------------------------------------------------------------------

// Shaders and pipelines found while testing: the player's first run stutters less.
void SeedShaderCache() {
  fs::path source = Root() / L"bin" / L"shader_cache";
  fs::path local = LocalAppData();
  if (!Exists(source) || local.empty()) return;
  fs::path target = local / L"AnyX360" / kUserDir / L"cache" / L"shaders";
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(source, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    fs::path destination = target / fs::relative(it->path(), source, ec);
    if (Exists(destination)) continue;  // never replace the player's own (larger) cache
    fs::create_directories(destination.parent_path(), ec);
    fs::copy_file(it->path(), destination, ec);
  }
}

// Starts the game; returns an error message, or one with no id on success.
Message StartGame() {
  fs::path exe = FindProgram(kGameExe);
  if (exe.empty()) return {kTextErrorNoExe, {kGameExe}, true};
  SeedShaderCache();
  std::error_code ec;
  fs::create_directories(LogDir(), ec);
  fs::path log_path = LogDir() / L"last_run.log";
  if (Exists(log_path)) {
    fs::remove(LogDir() / L"previous_run.log", ec);
    fs::rename(log_path, LogDir() / L"previous_run.log", ec);
  }
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr);
  std::wstring command = Quote(exe) + L" --game " + Quote(GameDir());
  fs::path key = Root() / L"keys" / L"xex2_retail.key";
  if (Exists(key)) command += L" --key " + Quote(key);
  STARTUPINFOW startup{sizeof(startup)};
  if (log != INVALID_HANDLE_VALUE) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = log;
    startup.hStdError = log;
  }
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                Root().c_str(), &startup, &process);
  DWORD error = GetLastError();
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  CloseHandle(input);
  if (!started) return {kTextErrorStart, {std::to_wstring(error)}, true};
  CloseHandle(process.hThread);
  std::thread([handle = process.hProcess] {
    WaitForSingleObject(handle, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(handle, &code);
    CloseHandle(handle);
    PostMessageW(g_window, kMsgGameExited, code, 0);
  }).detach();
  return {};
}

// ---- window -----------------------------------------------------------------------------------

void Layout();
bool ShowTerms(bool require_acceptance);

void UpdateUi() {
  bool ready = GameReady();
  std::wstring status;
  if (g_state == State::kPreparing)
    status = g_percent < 0      ? Tr(kTextChecking)
             : g_percent >= 100 ? Tr(kTextFinishing)
                                : Tr(kTextExtracting, {std::to_wstring(g_percent)});
  else
    status = ready ? Tr(kWebcam ? kTextReadyWebcam : kTextReadyPad) : Tr(kTextMissing, {kGameTitle, kGameSize});
  SetDlgItemTextW(g_window, kMessage, g_message.Render().c_str());
  SetDlgItemTextW(g_window, kStatus, status.c_str());
  ShowWindow(GetDlgItem(g_window, kMessage), g_message.id == kTextCount ? SW_HIDE : SW_SHOW);
  Layout();
  HWND progress = GetDlgItem(g_window, kProgress);
  ShowWindow(progress, g_state == State::kPreparing ? SW_SHOW : SW_HIDE);
  if (g_percent < 0) {
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) | PBS_MARQUEE);
    SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
  } else {
    SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) & ~LONG_PTR(PBS_MARQUEE));
    SendMessageW(progress, PBM_SETPOS, g_percent, 0);
  }
  EnableWindow(GetDlgItem(g_window, kPlay), g_state == State::kIdle && ready);
  SetDlgItemTextW(g_window, kPrepare,
                  Tr(g_state == State::kPreparing ? kTextCancel : ready ? kTextPrepareAgain : kTextChoose).c_str());
  EnableWindow(GetDlgItem(g_window, kLogs), Exists(LogDir()));
  SendMessageW(g_window, DM_SETDEFID, ready && g_state == State::kIdle ? kPlay : kPrepare, 0);
}

// The texts that don't depend on the state, after a language change.
void ApplyLanguage() {
  SetDlgItemTextW(g_window, kSubtitle, Tr(kTextSubtitle, {kVersion}).c_str());
  SetDlgItemTextW(g_window, kPlay, Tr(kTextPlay).c_str());
  SetDlgItemTextW(g_window, kLogs, Tr(kTextLogs).c_str());
  SetDlgItemTextW(g_window, kFooter, Tr(kTextFooter).c_str());
  UpdateUi();
}

fs::path ChooseArchive() {
  fs::path chosen;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return chosen;
  std::wstring filter_7z = Tr(kTextFilter7z), filter_all = Tr(kTextFilterAll);
  COMDLG_FILTERSPEC types[] = {{filter_7z.c_str(), L"*.7z"}, {filter_all.c_str(), L"*.*"}};
  dialog->SetFileTypes(2, types);
  std::wstring title = Tr(kTextDialogTitle, {kGameTitle});
  dialog->SetTitle(title.c_str());
  dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
  IShellItem* item = nullptr;
  if (SUCCEEDED(dialog->Show(g_window)) && SUCCEEDED(dialog->GetResult(&item))) {
    wchar_t* path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) chosen = path;
    CoTaskMemFree(path);
    item->Release();
  }
  dialog->Release();
  return chosen;
}

fs::path ChooseFolder() {
  fs::path chosen;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return chosen;
  std::wstring title = Tr(kTextFolderTitle, {kGameTitle});
  dialog->SetTitle(title.c_str());
  dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
  IShellItem* item = nullptr;
  if (SUCCEEDED(dialog->Show(g_window)) && SUCCEEDED(dialog->GetResult(&item))) {
    wchar_t* path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) chosen = path;
    CoTaskMemFree(path);
    item->Release();
  }
  dialog->Release();
  return chosen;
}

// `from_folder`: the player's folder, used in place; otherwise a .7z archive extracted here.
void StartPreparing(const fs::path& source, bool from_folder) {
  g_state = State::kPreparing;
  g_percent = -1;
  g_message = {};
  g_cancel = false;
  UpdateUi();
  g_worker = from_folder ? std::thread(PrepareFolderWorker, source) : std::thread(PrepareWorker, source);
}

void OnPrepare() {
  if (g_state == State::kPreparing) {  // Cancel
    g_cancel = true;
    std::lock_guard lock(g_extractor_mutex);
    if (g_extractor) TerminateProcess(g_extractor, 1);
    return;
  }
  if (GameReady() &&
      MessageBoxW(g_window, Tr(kTextConfirmPrepareAgain).c_str(), kGameTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;
  // Where the copy comes from: a menu under the button.
  enum { kFromArchive = 1, kFromFolder };
  std::wstring from_archive = Tr(kTextMenuArchive), from_folder = Tr(kTextMenuFolder);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kFromArchive, from_archive.c_str());
  AppendMenuW(menu, MF_STRING, kFromFolder, from_folder.c_str());
  RECT button;
  GetWindowRect(GetDlgItem(g_window, kPrepare), &button);
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, button.left,
                              button.bottom, 0, g_window, nullptr);
  DestroyMenu(menu);
  if (choice == kFromArchive) {
    if (SevenZip().empty()) {
      MessageBoxW(g_window, Tr(kTextMissing7Zip).c_str(), kGameTitle, MB_ICONERROR);
      return;
    }
    fs::path archive = ChooseArchive();
    if (!archive.empty()) StartPreparing(archive, false);
  } else if (choice == kFromFolder) {
    fs::path folder = ChooseFolder();
    if (!folder.empty()) StartPreparing(folder, true);
  }
}

void OnPlay() {
  Message error = StartGame();
  if (error.id != kTextCount) {
    g_message = error;
    UpdateUi();
    return;
  }
  g_state = State::kRunning;
  g_message = {};
  ShowWindow(g_window, SW_HIDE);
}

int Scale(int value) { return MulDiv(value, int(GetDpiForWindow(g_window)), 96); }

void CreateFonts(UINT dpi) {
  for (HFONT font : {g_title_font, g_text_font, g_small_font})
    if (font) DeleteObject(font);
  auto make = [dpi](int points, int weight) {
    return CreateFontW(-MulDiv(points, int(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  };
  g_title_font = make(18, FW_SEMIBOLD);
  g_text_font = make(10, FW_NORMAL);
  g_small_font = make(9, FW_NORMAL);
  for (int id : {kTitle, kSubtitle, kStatus, kMessage, kPlay, kPrepare, kLogs, kFooter}) {
    HFONT font = id == kTitle ? g_title_font : id == kSubtitle || id == kFooter ? g_small_font : g_text_font;
    SendDlgItemMessageW(g_window, id, WM_SETFONT, WPARAM(font), TRUE);
  }
}

constexpr int kClientWidth = 540, kClientHeight = 298;

void Layout() {
  struct Item {
    int id, x, y, w, h;
  };
  // The outcome of the last action, when there is one, goes above the status.
  int status_y = g_message.id == kTextCount ? 90 : 136;
  const Item items[] = {{kTitle, 24, 18, 492, 36},        {kSubtitle, 24, 56, 492, 20},  {kMessage, 24, 90, 492, 42},
                        {kStatus, 24, status_y, 492, 46}, {kProgress, 24, 186, 492, 16}, {kPlay, 24, 214, 150, 36},
                        {kPrepare, 186, 214, 190, 36},    {kLogs, 388, 214, 128, 36},    {kFooter, 24, 266, 492, 20}};
  for (const Item& item : items)
    SetWindowPos(GetDlgItem(g_window, item.id), nullptr, Scale(item.x), Scale(item.y), Scale(item.w), Scale(item.h),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g_window, HMENU(INT_PTR(id)), instance,
                    nullptr);
  };
  add(L"STATIC", kGameTitle, SS_LEFT | SS_NOPREFIX, kTitle);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kSubtitle);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kStatus);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kMessage);
  add(PROGRESS_CLASSW, L"", 0, kProgress);
  add(L"BUTTON", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, kPlay);
  add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, kPrepare);
  add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, kLogs);
  add(WC_LINK, L"", WS_TABSTOP, kFooter);
  SendDlgItemMessageW(g_window, kProgress, PBM_SETRANGE32, 0, 100);
}

// ---- terms window ------------------------------------------------------------------------------

struct TermsWindow {
  HWND hwnd = nullptr;
  bool accepted = false;
  bool done = false;
} g_terms;

LRESULT CALLBACK TermsProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kTermsCheck:
          EnableWindow(GetDlgItem(hwnd, kTermsAccept), IsDlgButtonChecked(hwnd, kTermsCheck) == BST_CHECKED);
          return 0;
        case kTermsAccept:
          g_terms.accepted = true;
          DestroyWindow(hwnd);
          return 0;
        case kTermsDecline:
        case IDCANCEL:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    case WM_CTLCOLORSTATIC: {  // also the read-only text box
      HDC dc = HDC(wparam);
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, RGB(25, 25, 25));
      return LRESULT(GetStockObject(WHITE_BRUSH));
    }
    case WM_DESTROY:
      g_terms.done = true;
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Shows the terms of use in the current language. With `require_acceptance` the player must tick
// "I accept" and press Accept (remembered); otherwise it's just for reading. Returns whether they
// were accepted.
bool ShowTerms(bool require_acceptance) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  static bool registered = [&] {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = TermsProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = L"AnyX360Terms";
    return RegisterClassExW(&wc) != 0;
  }();
  (void)registered;
  g_terms = {};
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  RECT frame{0, 0, Scale(640), Scale(530)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, GetDpiForWindow(g_window));
  int width = frame.right - frame.left, height = frame.bottom - frame.top;
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
  std::wstring title = Tr(kTextTermsTitle, {kGameTitle});
  g_terms.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AnyX360Terms", title.c_str(), style,
                                 (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
                                 (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height,
                                 IsWindowVisible(g_window) ? g_window : nullptr, nullptr, instance, nullptr);
  auto add = [&](const wchar_t* cls, const std::wstring& text, DWORD control_style, int id, int x, int y, int w,
                 int h, HFONT font) {
    HWND control = CreateWindowExW(0, cls, text.c_str(), WS_CHILD | WS_VISIBLE | control_style, Scale(x), Scale(y),
                                   Scale(w), Scale(h), g_terms.hwnd, HMENU(INT_PTR(id)), instance, nullptr);
    SendMessageW(control, WM_SETFONT, WPARAM(font), TRUE);
    return control;
  };
  add(L"STATIC", Tr(kTextTermsHeading), SS_LEFT | SS_NOPREFIX, kTermsHeading, 24, 16, 592, 32, g_title_font);
  add(L"EDIT", TermsText(), WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP,
      kTermsText, 24, 58, 592, 370, g_text_font);
  HWND focus;
  if (require_acceptance) {
    focus = add(L"BUTTON", Tr(kTextTermsCheck), BS_AUTOCHECKBOX | WS_TABSTOP, kTermsCheck, 24, 440, 592, 24,
                g_text_font);
    HWND accept = add(L"BUTTON", Tr(kTextTermsAccept), BS_DEFPUSHBUTTON | WS_TABSTOP, kTermsAccept, 380, 480, 112,
                      34, g_text_font);
    EnableWindow(accept, FALSE);
    add(L"BUTTON", Tr(kTextTermsDecline), BS_PUSHBUTTON | WS_TABSTOP, kTermsDecline, 504, 480, 112, 34, g_text_font);
  } else {
    focus = add(L"BUTTON", Tr(kTextTermsClose), BS_DEFPUSHBUTTON | WS_TABSTOP, kTermsDecline, 504, 480, 112, 34,
                g_text_font);
  }
  bool owner_enabled = IsWindowVisible(g_window);
  if (owner_enabled) EnableWindow(g_window, FALSE);
  ShowWindow(g_terms.hwnd, SW_SHOW);
  SetForegroundWindow(g_terms.hwnd);
  SetFocus(focus);
  MSG msg;
  while (!g_terms.done) {
    BOOL got = GetMessageW(&msg, nullptr, 0, 0);
    if (got <= 0) {
      if (got == 0) PostQuitMessage(int(msg.wParam));  // leave WM_QUIT for the main loop
      break;
    }
    if (IsDialogMessageW(g_terms.hwnd, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (owner_enabled) {
    EnableWindow(g_window, TRUE);
    SetForegroundWindow(g_window);
  }
  if (require_acceptance && g_terms.accepted) WriteSetting(GameSettingsFile(L"terms_accepted.txt"), TermsHash());
  return g_terms.accepted;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_NOTIFY: {
      const NMLINK* link = reinterpret_cast<const NMLINK*>(lparam);
      if (link->hdr.idFrom != kFooter || (link->hdr.code != NM_CLICK && link->hdr.code != NM_RETURN)) break;
      if (link->item.iLink == 0) {
        ShowTerms(false);
      } else {  // the other language
        g_language = g_language == Language::kSpanish ? Language::kEnglish : Language::kSpanish;
        WriteSetting(SettingsFile(L"launcher_language.txt"), g_language == Language::kSpanish ? L"es" : L"en");
        ApplyLanguage();
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kPlay: OnPlay(); return 0;
        case kPrepare: OnPrepare(); return 0;
        case kLogs: ShellExecuteW(hwnd, L"open", LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
      }
      break;
    case kMsgProgress:
      g_percent = int(wparam);
      UpdateUi();
      return 0;
    case kMsgPrepared: {
      if (g_worker.joinable()) g_worker.join();
      std::unique_ptr<Message> error(reinterpret_cast<Message*>(lparam));
      g_state = State::kIdle;
      g_message = error ? *error : Message{g_cancel ? kTextCancelled : kTextPrepared, {}, false};
      g_cancel = false;
      if (g_close_after_cancel) {
        DestroyWindow(hwnd);
        return 0;
      }
      UpdateUi();
      return 0;
    }
    case kMsgGameExited: {
      if (wparam == 0) {  // closed normally (or restarted from the game's settings panel)
        DestroyWindow(hwnd);
        return 0;
      }
      g_state = State::kIdle;
      wchar_t code[16];
      swprintf(code, 16, DWORD(wparam) > 255 ? L"0x%08X" : L"%u", DWORD(wparam));
      g_message = {kTextErrorExited, {code}, true};
      UpdateUi();
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = HDC(wparam);
      int id = GetDlgCtrlID(HWND(lparam));
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, id == kSubtitle || id == kFooter          ? RGB(110, 110, 110)
                       : id == kMessage && g_message.error    ? RGB(170, 30, 30)
                       : id == kMessage                       ? RGB(20, 110, 40)
                                                              : RGB(25, 25, 25));
      return LRESULT(GetStockObject(WHITE_BRUSH));
    }
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      CreateFonts(HIWORD(wparam));
      Layout();
      return 0;
    }
    case WM_CLOSE:
      if (g_state == State::kPreparing) {
        if (MessageBoxW(hwnd, Tr(kTextConfirmCancelExit).c_str(), kGameTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
          return 0;
        g_close_after_cancel = true;
        OnPrepare();  // cancels; the window closes when the worker has cleaned up
        return 0;
      }
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES | ICC_LINK_CLASS};
  InitCommonControlsEx(&controls);
  LoadLanguage();

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = L"AnyX360Launcher";
  RegisterClassExW(&wc);
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  std::wstring title = std::wstring(kGameTitle) + L" (AnyX360)";
  g_window = CreateWindowExW(0, wc.lpszClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                             nullptr, nullptr, instance, nullptr);
  if (!g_window) return 1;

  // Size the client area for the window's DPI and center it on its monitor.
  UINT dpi = GetDpiForWindow(g_window);
  RECT frame{0, 0, Scale(kClientWidth), Scale(kClientHeight)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
  int width = frame.right - frame.left, height = frame.bottom - frame.top;
  SetWindowPos(g_window, nullptr, (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
               (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height, SWP_NOZORDER);
  CreateControls();
  CreateFonts(dpi);
  ApplyLanguage();
  if (!TermsAccepted() && !ShowTerms(true)) {
    DestroyWindow(g_window);
    CoUninitialize();
    return 0;
  }
  // A copy prepared in place that has since been moved or deleted (or a disconnected drive).
  fs::path stored = StoredLocation();
  if (!GameReady() && !stored.empty()) {
    g_message = {kTextErrorLocationGone, {ShortPath(stored)}, true};
    UpdateUi();
  }
  ShowWindow(g_window, show);
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i + 1 < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--archive" && !SevenZip().empty()) StartPreparing(argv[i + 1], false);
    if (arg == L"--folder") StartPreparing(argv[i + 1], true);
  }
  LocalFree(argv);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g_window, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g_worker.joinable()) g_worker.join();
  CoUninitialize();
  return 0;
}
