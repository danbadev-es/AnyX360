// AnyX360's launcher (AnyX360.exe): a gallery of the games the player has added to the release, from
// their own copies, and the place to start them. A game is added from its disc archive (.7z, .rar or
// .zip, extracted here) or from the folder where the copy already is; which game it is comes from
// default.xex's title ID, matched against the games this release was built with (games_catalog.h).
//
//   AnyX360.exe                 this program
//   bin\<game>.exe (+ .pdb)     each recompiled game (the .pdb names functions in crash reports)
//   bin\7z.exe, bin\7z.dll      7-Zip, to extract archives
//   bin\shader_cache\<id>\      Xenia shader storage, copied once to the player's cache
//   keys\xex2_retail.key
//   games\<id>\game\            a copy extracted from an archive (the folder holding default.xex)
//   games\<id>\location.txt     or the folder of the player's copy, used where it is (UTF-8)
//   games\<id>\icon.png         the game's dashboard icon, read from default.xex when it is added
//   games\<id>\cover.png|.jpg   a cover the player chose instead
//   logs\<id>\                  last_run.log (the game's output) and previous_run.log
//
// A copy must be one of the catalog's games and complete: default.xex's title ID and the game's
// required files are checked. Run from the build folder the games are found where the build puts
// them. `--archive <file>` and `--folder <dir>` add a game without asking (unattended tests),
// `--select <id>` selects one.
//
// The text is in English and Spanish: Windows' display language picks one, the footer switches
// (remembered in %LOCALAPPDATA%\AnyX360\launcher_language.txt). The terms of use
// (tools/launcher/terms_en.txt and terms_es.txt naming every game, compiled in) must be accepted
// before the first use, and again whenever either text changes (%LOCALAPPDATA%\AnyX360\
// terms_accepted.txt holds a hash).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
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

namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "anyx360_version.h"
#include "games_catalog.h"
#include "launcher_terms.h"
#include "util.h"
#include "xdbf.h"
#include "xex.h"

namespace fs = std::filesystem;
namespace gp = Gdiplus;

#define ANYX360_WIDEN2(x) L##x
#define ANYX360_WIDEN(x) ANYX360_WIDEN2(x)

namespace {

const wchar_t* const kVersion = ANYX360_WIDEN(ANYX360_VERSION_FULL);

constexpr UINT kMsgProgress = WM_APP + 1;    // wParam: percent (-1 while checking)
constexpr UINT kMsgDone = WM_APP + 2;        // lParam: Outcome* of the job
constexpr UINT kMsgGameExited = WM_APP + 3;  // wParam: exit code
constexpr UINT kMsgAskReplace = WM_APP + 4;  // lParam: const CatalogGame*; returns IDYES to replace it
constexpr UINT kMsgJobGame = WM_APP + 5;     // lParam: const CatalogGame* the job turned out to be
constexpr UINT kMsgIconReady = WM_APP + 6;   // a game's icon was read

enum Control { kTitle = 100, kSubtitle, kGallery, kSupported, kMessage, kStatus, kProgress, kPlay, kAdd, kLogs, kFooter };
enum TermsControl { kTermsHeading = 200, kTermsText, kTermsCheck, kTermsAccept, kTermsDecline };
enum MenuCommand { kMenuArchive = 1, kMenuFolder, kMenuPlay, kMenuCover, kMenuRestoreCover, kMenuLogs, kMenuPrepareAgain,
                   kMenuRemove };
enum class State { kIdle, kWorking, kRunning };

// ---- text -------------------------------------------------------------------------------------

enum class Language { kEnglish, kSpanish };
Language g_language = Language::kEnglish;

enum Text {
  kTextSubtitle,  // {0} version
  kTextChecking,
  kTextFinishing,
  kTextExtracting,  // {0} game, {1} percent
  kTextRemoving,    // {0} game
  kTextReadyPad,    // {0} game
  kTextReadyWebcam,  // {0} game
  kTextAddHint,     // {0} supported games
  kTextAlsoSupported,  // {0} games
  kTextPlay,
  kTextAddGame,
  kTextAddTile,
  kTextCancel,
  kTextLogs,
  kTextFooter,
  kTextMenuArchive,
  kTextMenuFolder,
  kTextMenuPlay,
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
  kTextErrorList,         // {0} 7-Zip exit code
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
  kTextTermsTitle,
  kTextTermsHeading,
  kTextTermsCheck,
  kTextTermsAccept,
  kTextTermsDecline,
  kTextTermsClose,
  kTextCount
};

const wchar_t* const kEnglish[] = {
    L"{0} · Xbox 360 games recompiled for PC",
    L"Checking the game…",
    L"Finishing…",
    L"Extracting {0}… {1} %",
    L"Removing {0}…",
    L"{0} is ready to play. Keyboard and mouse or an Xbox controller; F2 opens the settings in game.",
    L"{0} is ready to play. Stand 2-3 m from your webcam with your whole body in view; F2 opens the "
    L"settings in game.",
    L"Add a game: choose the archive of your disc (.7z, .rar or .zip, it is extracted into this folder) or "
    L"the folder where your copy already is. This release plays {0}.",
    L"Also supported: {0}",
    L"Play",
    L"Add game…",
    L"Add game",
    L"Cancel",
    L"Logs",
    L"Developed by danbadev-es  ·  <a>Terms of use</a>  ·  <a>Español</a>",
    L"From an archive (.7z, .rar, .zip)…",
    L"From a game folder…",
    L"Play",
    L"Change cover…",
    L"Restore the game's cover",
    L"Open logs",
    L"Prepare again…",
    L"Remove from the library…",
    L"Choose the archive of your game (.7z, .rar or .zip)",
    L"Choose the folder of your game (the one with default.xex)",
    L"Choose an image for {0}",
    L"Game archive (.7z, .rar, .zip)",
    L"Images",
    L"All files",
    L"7-Zip is missing (bin\\7z.exe). Extract the whole folder again.",
    L"{0} is already in the library. Replace it with this copy?",
    L"Remove {0} from the library? A copy extracted into the games folder is deleted; a folder of yours is "
    L"left as it is. Your saves and settings stay.",
    L"Cancel and exit?",
    L"{0} is ready.",
    L"{0} is ready. It is played from {1}: don't move or delete that folder.",
    L"{0} was removed from the library.",
    L"Cancelled.",
    L"7-Zip could not read the archive (code {0}). Check that it is the game's archive and that it is complete.",
    L"That archive has no default.xex: it doesn't look like an Xbox 360 game disc.",
    L"That folder has no default.xex: choose the folder of your copy of the game.",
    L"That copy is not one of this release's games (title {0}). It plays {1}.",
    L"The copy is incomplete: {0} is missing.",
    L"The folder of {0} is no longer available ({1}). Add the game again, or remove it from the library.",
    L"Not enough space on this drive: {0} needed, {1} free. Move this folder to another drive or free up "
    L"space.",
    L"The extraction failed (7-Zip code {0}). Check the free space and that the archive isn't damaged.",
    L"default.xex was not found after extracting.",
    L"The game could not be moved to the games folder ({0}).",
    L"{0} was not found in the bin folder.",
    L"The game could not be started (error {0}).",
    L"{0} closed with an error (code {1}). Press Logs and send the last_run.log file to whoever gave you "
    L"the game.",
    L"That image could not be opened.",
    L"Terms of use (AnyX360)",
    L"Terms of use",
    L"I have read and accept the terms of use",
    L"Accept",
    L"Decline",
    L"Close",
};

const wchar_t* const kSpanish[] = {
    L"{0} · juegos de Xbox 360 recompilados para PC",
    L"Comprobando el juego…",
    L"Terminando…",
    L"Extrayendo {0}… {1} %",
    L"Quitando {0}…",
    L"{0} está listo para jugar. Teclado y ratón o mando de Xbox; F2 abre los ajustes durante la partida.",
    L"{0} está listo para jugar. Ponte a 2-3 m de la webcam, con todo el cuerpo a la vista; F2 abre los "
    L"ajustes durante la partida.",
    L"Añade un juego: elige el archivo comprimido de tu disco (.7z, .rar o .zip, se extrae en esta carpeta) o "
    L"la carpeta donde ya tienes tu copia. Esta versión juega a {0}.",
    L"También compatible: {0}",
    L"Jugar",
    L"Añadir juego…",
    L"Añadir juego",
    L"Cancelar",
    L"Registros",
    L"Desarrollado por danbadev-es  ·  <a>Términos y condiciones de uso</a>  ·  <a>English</a>",
    L"Desde un archivo comprimido (.7z, .rar, .zip)…",
    L"Desde una carpeta del juego…",
    L"Jugar",
    L"Cambiar portada…",
    L"Recuperar la portada del juego",
    L"Abrir registros",
    L"Volver a preparar…",
    L"Quitar de la biblioteca…",
    L"Elige el archivo comprimido de tu juego (.7z, .rar o .zip)",
    L"Elige la carpeta de tu juego (la que tiene default.xex)",
    L"Elige una imagen para {0}",
    L"Archivo del juego (.7z, .rar, .zip)",
    L"Imágenes",
    L"Todos los archivos",
    L"Falta 7-Zip (bin\\7z.exe). Vuelve a descomprimir la carpeta completa.",
    L"{0} ya está en la biblioteca. ¿Reemplazarlo por esta copia?",
    L"¿Quitar {0} de la biblioteca? Se borra la copia extraída en la carpeta games; una carpeta tuya se queda "
    L"como está. Tus partidas guardadas y ajustes se conservan.",
    L"¿Cancelar y salir?",
    L"{0} está preparado.",
    L"{0} está preparado. Se juega desde {1}: no muevas ni borres esa carpeta.",
    L"{0} se ha quitado de la biblioteca.",
    L"Cancelado.",
    L"7-Zip no pudo leer el archivo (código {0}). Comprueba que es el archivo del juego y que está completo.",
    L"Ese archivo no contiene default.xex: no parece un disco de un juego de Xbox 360.",
    L"Esa carpeta no contiene default.xex: elige la carpeta de tu copia del juego.",
    L"Esa copia no es de ninguno de los juegos de esta versión (título {0}). Juega a {1}.",
    L"La copia está incompleta: falta {0}.",
    L"La carpeta de {0} ya no está disponible ({1}). Vuelve a añadir el juego o quítalo de la biblioteca.",
    L"No hay espacio suficiente en esta unidad: hacen falta {0} y hay {1}. Mueve esta carpeta a otro disco "
    L"o libera espacio.",
    L"La extracción falló (código de 7-Zip {0}). Comprueba el espacio libre y que el archivo no esté dañado.",
    L"Después de extraer no se encontró default.xex.",
    L"No se pudo mover el juego a la carpeta games ({0}).",
    L"No se encuentra {0} en la carpeta bin.",
    L"No se pudo iniciar el juego (error {0}).",
    L"{0} se cerró con un error (código {1}). Pulsa Registros y envía el archivo last_run.log a quien te "
    L"pasó el juego.",
    L"No se pudo abrir esa imagen.",
    L"Términos y condiciones de uso (AnyX360)",
    L"Términos y condiciones de uso",
    L"He leído y acepto los términos y condiciones de uso",
    L"Aceptar",
    L"No acepto",
    L"Cerrar",
};
static_assert(std::size(kEnglish) == kTextCount && std::size(kSpanish) == kTextCount);

std::wstring Format(std::wstring text, const std::vector<std::wstring>& args) {
  for (size_t i = 0; i < args.size(); ++i) {
    std::wstring marker = L"{" + std::to_wstring(i) + L"}";
    for (size_t at = 0; (at = text.find(marker, at)) != std::wstring::npos; at += args[i].size())
      text.replace(at, marker.size(), args[i]);
  }
  return text;
}

// A text in the current language with {0}, {1}... replaced.
std::wstring Tr(Text id, const std::vector<std::wstring>& args = {}) {
  return Format((g_language == Language::kSpanish ? kSpanish : kEnglish)[id], args);
}

// The outcome of the last action, kept untranslated so it follows language changes.
struct Message {
  Text id = kTextCount;  // none
  std::vector<std::wstring> args;
  bool error = false;
  std::wstring Render() const { return id == kTextCount ? std::wstring() : Tr(id, args); }
};

// What a job (adding or removing a game) ends with.
struct Outcome {
  Message message;
  std::wstring select;  // the game to select afterwards
};

// ---- state ------------------------------------------------------------------------------------

// A catalog game whose executable this release has, with what the player has done with it.
struct Game {
  const CatalogGame* info = nullptr;
  fs::path exe;
  bool installed = false;
  fs::path dir;               // the folder holding default.xex
  bool location_gone = false;  // added from a folder that is no longer there
  std::unique_ptr<gp::Bitmap> icon, cover;
  gp::Color color{255, 62, 70, 92};
};

HWND g_window = nullptr;
HWND g_gallery = nullptr;
HFONT g_title_font = nullptr, g_text_font = nullptr, g_small_font = nullptr;
std::vector<Game> g_games;     // the catalog games this release has, in catalog order
std::vector<int> g_tiles;      // the gallery: indexes of installed games, then -1 for "Add game"
int g_selected = 0;            // index into g_tiles
int g_hover = -1;
int g_scroll = 0;
State g_state = State::kIdle;
Message g_message;  // shown above the status
int g_percent = -1;
const CatalogGame* g_job_game = nullptr;  // the game being added or removed, once known
std::thread g_worker;
std::atomic<bool> g_cancel{false};
bool g_close_after_cancel = false;
std::mutex g_extractor_mutex;
HANDLE g_extractor = nullptr;  // the running 7-Zip process
const CatalogGame* g_running = nullptr;

// ---- paths ------------------------------------------------------------------------------------

fs::path Root() {
  static const fs::path root = [] {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
  }();
  return root;
}

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

fs::path GamesDir() { return Root() / L"games"; }
fs::path GameRoot(const CatalogGame& game) { return GamesDir() / game.id; }
fs::path ExtractedDir(const CatalogGame& game) { return GameRoot(game) / L"game"; }
fs::path LocationFile(const CatalogGame& game) { return GameRoot(game) / L"location.txt"; }
fs::path IconFile(const CatalogGame& game) { return GameRoot(game) / L"icon.png"; }
fs::path LogDir(const CatalogGame& game) { return Root() / L"logs" / game.id; }

// The player's cover for the game, if they chose one.
fs::path CoverFile(const CatalogGame& game) {
  for (const wchar_t* name : {L"cover.png", L"cover.jpg"})
    if (Exists(GameRoot(game) / name)) return GameRoot(game) / name;
  return {};
}

// `relative` in `start` or the closest folder above it that has it (the build folder is inside the
// repository, next to keys\).
fs::path FindUpwards(fs::path start, const fs::path& relative) {
  for (fs::path dir = start; !dir.empty(); dir = dir.parent_path()) {
    if (Exists(dir / relative)) return dir / relative;
    if (dir == dir.root_path()) break;
  }
  return {};
}

fs::path KeyFile() { return FindUpwards(Root(), L"keys/xex2_retail.key"); }

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

// The folder of a copy added in place, or empty.
fs::path StoredLocation(const CatalogGame& game) {
  return Exists(LocationFile(game)) ? fs::path(ReadUtf8(LocationFile(game))) : fs::path();
}

// bin\ in a release, next to the launcher otherwise.
fs::path FindProgram(const wchar_t* name) {
  for (const fs::path& path : {Root() / L"bin" / name, Root() / name})
    if (Exists(path)) return path;
  return {};
}

// The game's executable: the release's, else (running from the build folder) the build's.
fs::path FindGameExe(const CatalogGame& game) {
  fs::path exe = FindProgram(game.exe);
  if (exe.empty() && Exists(game.dev_exe)) exe = fs::path(game.dev_exe).make_preferred();
  return exe;
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

// The launcher's settings (the games keep theirs, and their saves, in their own folders there).
fs::path SettingsFile(const wchar_t* name) { return LocalAppData() / L"AnyX360" / name; }

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

std::wstring Hex32(uint32_t value) {
  wchar_t text[16];
  swprintf(text, 16, L"%08X", value);
  return text;
}

void LoadLanguage() {
  std::wstring saved = ReadSetting(SettingsFile(L"launcher_language.txt"));
  if (saved == L"es" || saved == L"en")
    g_language = saved == L"es" ? Language::kSpanish : Language::kEnglish;
  else
    g_language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH ? Language::kSpanish : Language::kEnglish;
}

// "a", "a and b", "a, b and c" in the current language.
std::wstring JoinTitles(const std::vector<const CatalogGame*>& games) {
  std::wstring text;
  for (size_t i = 0; i < games.size(); ++i) {
    if (i) text += i + 1 == games.size() ? (g_language == Language::kSpanish ? L" y " : L" and ") : L", ";
    text += games[i]->title;
  }
  return text;
}

// The catalog games this release can run (their executable is there).
std::vector<const CatalogGame*> AvailableGames() {
  std::vector<const CatalogGame*> games;
  for (const CatalogGame& game : kCatalog)
    if (!FindGameExe(game).empty()) games.push_back(&game);
  return games;
}

const CatalogGame* FindAvailableGame(uint32_t title_id) {
  for (const CatalogGame* game : AvailableGames())
    if (game->title_id == title_id) return game;
  return nullptr;
}

bool IsInstalled(const CatalogGame& game) {
  return Exists(ExtractedDir(game) / L"default.xex") || Exists(LocationFile(game));
}

// ---- images -----------------------------------------------------------------------------------

// An image file decoded into memory (GDI+ keeps a file it loads open, and a cover can be replaced).
std::unique_ptr<gp::Bitmap> LoadImage(const fs::path& path) {
  std::vector<uint8_t> bytes;
  try {
    bytes = x360tool::read_file(path);
  } catch (const std::exception&) {
    return nullptr;
  }
  IStream* stream = SHCreateMemStream(bytes.data(), UINT(bytes.size()));
  if (!stream) return nullptr;
  std::unique_ptr<gp::Bitmap> decoded(gp::Bitmap::FromStream(stream));
  std::unique_ptr<gp::Bitmap> copy;
  if (decoded && decoded->GetLastStatus() == gp::Ok && decoded->GetWidth() && decoded->GetHeight()) {
    copy = std::make_unique<gp::Bitmap>(INT(decoded->GetWidth()), INT(decoded->GetHeight()), PixelFormat32bppPARGB);
    gp::Graphics graphics(copy.get());
    graphics.DrawImage(decoded.get(), 0, 0, INT(decoded->GetWidth()), INT(decoded->GetHeight()));
  }
  decoded.reset();
  stream->Release();
  return copy;
}

// The color a tile is painted with: the average of the icon's colorful pixels, kept dark enough for
// white text.
gp::Color TileColor(gp::Bitmap* icon) {
  gp::Color fallback(255, 62, 70, 92);
  if (!icon) return fallback;
  gp::Rect rect(0, 0, INT(icon->GetWidth()), INT(icon->GetHeight()));
  gp::BitmapData data;
  if (icon->LockBits(&rect, gp::ImageLockModeRead, PixelFormat32bppARGB, &data) != gp::Ok) return fallback;
  double sum[3] = {}, weight = 0;
  for (UINT y = 0; y < data.Height; ++y) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data.Scan0) + y * data.Stride);
    for (UINT x = 0; x < data.Width; ++x) {
      uint32_t argb = row[x];
      if ((argb >> 24) < 128) continue;
      double r = (argb >> 16 & 0xFF) / 255.0, g = (argb >> 8 & 0xFF) / 255.0, b = (argb & 0xFF) / 255.0;
      double high = std::max({r, g, b}), low = std::min({r, g, b});
      double saturation = high > 0 ? (high - low) / high : 0;
      double w = saturation * saturation * high + 0.01;
      sum[0] += r * w, sum[1] += g * w, sum[2] += b * w, weight += w;
    }
  }
  icon->UnlockBits(&data);
  if (weight <= 0) return fallback;
  double r = sum[0] / weight, g = sum[1] / weight, b = sum[2] / weight;
  // Scale the brightest channel to 0.55 (dark enough for white text, still colorful).
  double high = std::max({r, g, b, 0.01});
  double scale = 0.55 / high;
  auto channel = [&](double v) { return BYTE(std::clamp(v * scale, 0.0, 1.0) * 255.0 + 0.5); };
  return gp::Color(255, channel(r), channel(g), channel(b));
}

// Reads the dashboard icon from the copy's default.xex into games\<id>\icon.png.
bool SaveIcon(const CatalogGame& game, const fs::path& game_dir) {
  try {
    x360tool::Xex xex(x360tool::read_file(game_dir / L"default.xex"));
    fs::path key = KeyFile();
    auto image = xex.extract_image(key.empty() ? std::nullopt : x360tool::load_key(key, false));
    auto png = x360tool::xex_title_image(xex, image);
    if (!png) return false;
    std::error_code ec;
    fs::create_directories(GameRoot(game), ec);
    x360tool::write_file(IconFile(game), *png);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ---- the library ------------------------------------------------------------------------------

int SelectedGame() {
  return g_selected >= 0 && g_selected < int(g_tiles.size()) ? g_tiles[size_t(g_selected)] : -1;
}

// Reads what the player has added; keeps the selection on the same game (or `select`).
void LoadLibrary(std::wstring select = {}) {
  if (select.empty() && SelectedGame() >= 0) select = g_games[size_t(SelectedGame())].info->id;
  g_games.clear();
  for (const CatalogGame& info : kCatalog) {
    Game game;
    game.info = &info;
    game.exe = FindGameExe(info);
    if (game.exe.empty()) continue;
    if (Exists(ExtractedDir(info) / L"default.xex")) {
      game.installed = true;
      game.dir = ExtractedDir(info);
    } else if (Exists(LocationFile(info))) {
      game.installed = true;
      game.dir = StoredLocation(info);
      game.location_gone = !Exists(game.dir / L"default.xex");
    }
    if (game.installed) {
      game.icon = LoadImage(IconFile(info));
      fs::path cover = CoverFile(info);
      if (!cover.empty()) game.cover = LoadImage(cover);
      game.color = TileColor(game.icon.get());
      // A copy added before icons were read: read it now, the tile is redrawn when it is ready.
      if (!game.icon && !game.location_gone && !Exists(IconFile(info))) {
        std::thread([info = &info, dir = game.dir] {
          if (SaveIcon(*info, dir)) PostMessageW(g_window, kMsgIconReady, 0, 0);
        }).detach();
      }
    }
    g_games.push_back(std::move(game));
  }
  g_tiles.clear();
  for (size_t i = 0; i < g_games.size(); ++i)
    if (g_games[i].installed) g_tiles.push_back(int(i));
  g_tiles.push_back(-1);
  g_selected = 0;
  for (size_t t = 0; t < g_tiles.size(); ++t)
    if (g_tiles[t] >= 0 && g_games[size_t(g_tiles[t])].info->id == select) g_selected = int(t);
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

// Removes a folder quickly: renamed out of the way first, so a half-deleted copy never looks ready.
void RemoveFolder(const fs::path& path) {
  if (!Exists(path)) return;
  std::error_code ec;
  fs::path old = GamesDir() / (L".old-" + path.filename().wstring());
  RemoveAll(old);
  fs::rename(path, old, ec);
  RemoveAll(ec ? path : old);
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

// Which of this release's games the copy in `root` is, or an error.
const CatalogGame* IdentifyCopy(const fs::path& root, Message& error) {
  uint32_t title_id = 0;
  if (!ReadTitleId(root / L"default.xex", title_id)) {
    error = {kTextErrorNotGame, {}, true};
    return nullptr;
  }
  const CatalogGame* game = FindAvailableGame(title_id);
  if (!game) error = {kTextErrorUnsupported, {Hex32(title_id), JoinTitles(AvailableGames())}, true};
  return game;
}

// A copy must be complete: the game's required files are there.
bool CheckCopy(const fs::path& root, const CatalogGame& game, Message& error) {
  std::wstring files = game.required_files;
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

// Asks the player (on the window's thread) whether an installed game is replaced.
bool ConfirmReplace(const CatalogGame& game) {
  return !IsInstalled(game) || SendMessageW(g_window, kMsgAskReplace, 0, LPARAM(&game)) == IDYES;
}

void PostDone(Outcome outcome) { PostMessageW(g_window, kMsgDone, 0, LPARAM(new Outcome(std::move(outcome)))); }

// Worker thread: identifies the game in the archive (only default.xex is extracted for that),
// extracts it into games\.partial and moves the folder holding default.xex to games\<id>\game.
void AddArchiveWorker(fs::path archive) {
  ArchiveInfo info;
  Message error;
  if (!ListArchive(archive, info, error)) return PostDone({error});
  if (!info.has_game) return PostDone({{kTextErrorNotGame, {}, true}});

  fs::path probe = GamesDir() / L".probe";
  RemoveAll(probe);
  int code = RunCaptured(Quote(SevenZip()) + L" x -y -r -bso0 -bse1 -sccUTF-8 -o" + Quote(probe) + L" " +
                             Quote(archive) + L" default.xex",
                         [](const std::string&) {}, true);
  if (g_cancel) {
    RemoveAll(probe);
    return PostDone({});
  }
  fs::path probe_root = code == 0 ? FindGameRoot(probe) : fs::path();
  if (probe_root.empty()) {
    RemoveAll(probe);
    return PostDone({code == 0 ? Message{kTextErrorNotGame, {}, true}
                               : Message{kTextErrorExtract, {std::to_wstring(code)}, true}});
  }
  const CatalogGame* game = IdentifyCopy(probe_root, error);
  RemoveAll(probe);
  if (!game) return PostDone({error});
  if (!ConfirmReplace(*game)) return PostDone({{kTextCancelled, {}, false}});
  PostMessageW(g_window, kMsgJobGame, 0, LPARAM(game));

  ULARGE_INTEGER free_bytes{};
  uint64_t needed = info.size + (512ull << 20);
  if (GetDiskFreeSpaceExW(Root().c_str(), &free_bytes, nullptr, nullptr) && free_bytes.QuadPart < needed)
    return PostDone({{kTextErrorSpace, {Gigabytes(needed), Gigabytes(free_bytes.QuadPart)}, true}});

  fs::path partial = GamesDir() / L".partial";
  RemoveAll(partial);  // a previous attempt
  PostMessageW(g_window, kMsgProgress, 0, 0);
  int last = 0;
  std::string tail;
  code = RunCaptured(
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
        // Only forward: the bar fills once even if 7-Zip's count restarts (multi-volume archives).
        if (percent > last) {
          last = percent;
          PostMessageW(g_window, kMsgProgress, WPARAM(std::min(percent, 99)), 0);
        }
      },
      true);
  if (g_cancel) {
    RemoveAll(partial);
    return PostDone({});
  }
  if (code != 0) {
    RemoveAll(partial);
    return PostDone({{kTextErrorExtract, {std::to_wstring(code)}, true}});
  }
  PostMessageW(g_window, kMsgProgress, 100, 0);

  fs::path found = FindGameRoot(partial);
  if (found.empty()) {
    RemoveAll(partial);
    return PostDone({{kTextErrorNoXex, {}, true}});
  }
  if (IdentifyCopy(found, error) != game || !CheckCopy(found, *game, error)) {
    RemoveAll(partial);
    return PostDone({error});
  }
  // Replaces an earlier copy, extracted or used in place, but keeps the player's cover.
  RemoveFolder(ExtractedDir(*game));
  std::error_code ec;
  fs::remove(LocationFile(*game), ec);
  fs::create_directories(GameRoot(*game), ec);
  fs::rename(found, ExtractedDir(*game), ec);
  if (ec) {
    std::string reason = ec.message();
    RemoveAll(partial);
    return PostDone({{kTextErrorMove, {std::wstring(reason.begin(), reason.end())}, true}});
  }
  RemoveAll(partial);
  SaveIcon(*game, ExtractedDir(*game));
  PostDone({{kTextAdded, {game->title}, false}, game->id});
}

// Worker thread: identifies the copy in the player's folder and remembers it; it is played where it is.
void AddFolderWorker(fs::path folder) {
  fs::path root = FindGameRoot(folder);
  if (root.empty()) return PostDone({{kTextErrorNoXexFolder, {}, true}});
  Message error;
  const CatalogGame* game = IdentifyCopy(root, error);
  if (!game) return PostDone({error});
  PostMessageW(g_window, kMsgJobGame, 0, LPARAM(game));
  if (!CheckCopy(root, *game, error)) return PostDone({error});
  std::error_code ec;
  root = fs::absolute(root, ec);
  auto relative = root.lexically_relative(fs::absolute(ExtractedDir(*game), ec));
  if (!relative.empty() && *relative.begin() != L"..") {  // the extracted copy itself
    fs::remove(LocationFile(*game), ec);
    return PostDone({{kTextAdded, {game->title}, false}, game->id});
  }
  if (!ConfirmReplace(*game)) return PostDone({{kTextCancelled, {}, false}});
  RemoveFolder(ExtractedDir(*game));
  fs::create_directories(GameRoot(*game), ec);
  if (!WriteUtf8(LocationFile(*game), root.wstring())) return PostDone({{kTextErrorMove, {L"location.txt"}, true}});
  SaveIcon(*game, root);
  PostDone({{kTextAddedFolder, {game->title, ShortPath(root)}, false}, game->id});
}

// Worker thread: forgets a game. Only games\<id> is deleted: a folder of the player's is never in it.
void RemoveWorker(const CatalogGame* game) {
  RemoveFolder(GameRoot(*game));
  PostDone({{kTextRemoved, {game->title}, false}});
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

bool TermsAccepted() { return ReadSetting(SettingsFile(L"terms_accepted.txt")) == TermsHash(); }

// ---- running a game ---------------------------------------------------------------------------

// Shaders and pipelines found while testing: the player's first run stutters less.
void SeedShaderCache(const CatalogGame& game) {
  fs::path source = Root() / L"bin" / L"shader_cache" / game.id;
  fs::path local = LocalAppData();
  if (!Exists(source) || local.empty()) return;
  fs::path target = local / L"AnyX360" / game.user_dir / L"cache" / L"shaders";
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
Message StartGame(const Game& game) {
  if (game.exe.empty() || !Exists(game.exe)) return {kTextErrorNoExe, {game.info->exe}, true};
  SeedShaderCache(*game.info);
  std::error_code ec;
  fs::path logs = LogDir(*game.info);
  fs::create_directories(logs, ec);
  fs::path log_path = logs / L"last_run.log";
  if (Exists(log_path)) {
    fs::remove(logs / L"previous_run.log", ec);
    fs::rename(log_path, logs / L"previous_run.log", ec);
  }
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                             nullptr);
  std::wstring command = Quote(game.exe) + L" --game " + Quote(game.dir);
  fs::path key = KeyFile();
  if (!key.empty()) command += L" --key " + Quote(key);
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

// ---- gallery ----------------------------------------------------------------------------------

int Scale(int value) { return MulDiv(value, int(GetDpiForWindow(g_window)), 96); }
float ScaleF(float value) { return value * float(GetDpiForWindow(g_window)) / 96.0f; }

struct GalleryMetrics {
  int tile_w, tile_h, gap, pad, columns, content_h;
};

GalleryMetrics Metrics() {
  RECT client{};
  GetClientRect(g_gallery, &client);
  GalleryMetrics m{Scale(184), Scale(232), Scale(22), Scale(12), 1, 0};
  m.columns = std::max(1, (int(client.right) - 2 * m.pad + m.gap) / (m.tile_w + m.gap));
  int rows = (int(g_tiles.size()) + m.columns - 1) / m.columns;
  m.content_h = 2 * m.pad + rows * m.tile_h + std::max(0, rows - 1) * m.gap;
  return m;
}

RECT TileRect(int tile, const GalleryMetrics& m) {
  int column = tile % m.columns, row = tile / m.columns;
  int x = m.pad + column * (m.tile_w + m.gap), y = m.pad + row * (m.tile_h + m.gap) - g_scroll;
  return {x, y, x + m.tile_w, y + m.tile_h};
}

int TileAt(int x, int y) {
  GalleryMetrics m = Metrics();
  for (int t = 0; t < int(g_tiles.size()); ++t) {
    RECT r = TileRect(t, m);
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return t;
  }
  return -1;
}

void ClampScroll() {
  RECT client{};
  GetClientRect(g_gallery, &client);
  g_scroll = std::clamp(g_scroll, 0, std::max(0, Metrics().content_h - int(client.bottom)));
}

void EnsureVisible(int tile) {
  RECT client{};
  GetClientRect(g_gallery, &client);
  GalleryMetrics m = Metrics();
  RECT r = TileRect(tile, m);
  if (r.top < m.pad) g_scroll += r.top - m.pad;
  if (r.bottom > client.bottom - m.pad) g_scroll += r.bottom - (client.bottom - m.pad);
  ClampScroll();
}

void RoundedRect(gp::GraphicsPath& path, const gp::RectF& r, float radius) {
  float d = std::min({radius * 2, r.Width, r.Height});
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  path.CloseFigure();
}

gp::Color Shade(const gp::Color& c, float factor) {
  auto channel = [factor](BYTE v) { return BYTE(std::clamp(float(v) * factor, 0.0f, 255.0f)); };
  return gp::Color(c.GetA(), channel(c.GetR()), channel(c.GetG()), channel(c.GetB()));
}

// Draws `image` to fill `dest`, cropping what doesn't fit (like a poster).
void DrawCover(gp::Graphics& g, gp::Bitmap* image, const gp::RectF& dest) {
  float w = float(image->GetWidth()), h = float(image->GetHeight());
  float scale = std::max(dest.Width / w, dest.Height / h);
  float src_w = dest.Width / scale, src_h = dest.Height / scale;
  g.DrawImage(image, dest, (w - src_w) / 2, (h - src_h) / 2, src_w, src_h, gp::UnitPixel);
}

void DrawGameTile(gp::Graphics& g, const gp::RectF& r, const Game& game, bool hover) {
  gp::GraphicsPath tile;
  RoundedRect(tile, r, ScaleF(12));
  gp::LinearGradientBrush background(gp::PointF(r.X, r.Y), gp::PointF(r.X, r.GetBottom()), Shade(game.color, 1.25f),
                                     Shade(game.color, 0.6f));
  g.FillPath(&background, &tile);
  g.SetClip(&tile);
  float caption_h = ScaleF(54);
  gp::RectF picture(r.X, r.Y, r.Width, r.Height - caption_h);
  g.SetInterpolationMode(gp::InterpolationModeHighQualityBicubic);
  if (game.cover) {
    DrawCover(g, game.cover.get(), picture);
  } else if (game.icon) {
    float size = ScaleF(128);
    gp::RectF icon(picture.X + (picture.Width - size) / 2, picture.Y + (picture.Height - size) / 2 + ScaleF(4), size,
                   size);
    gp::GraphicsPath shadow;
    RoundedRect(shadow, gp::RectF(icon.X + ScaleF(3), icon.Y + ScaleF(5), size, size), ScaleF(10));
    gp::SolidBrush shadow_brush(gp::Color(70, 0, 0, 0));
    g.FillPath(&shadow_brush, &shadow);
    gp::GraphicsPath icon_path;
    RoundedRect(icon_path, icon, ScaleF(10));
    g.SetClip(&icon_path, gp::CombineModeIntersect);
    g.DrawImage(game.icon.get(), icon);
    g.SetClip(&tile);
  }
  gp::RectF caption(r.X, r.GetBottom() - caption_h, r.Width, caption_h);
  gp::SolidBrush caption_brush(gp::Color(game.cover ? 170 : 70, 0, 0, 0));
  g.FillRectangle(&caption_brush, caption);
  gp::Font font(L"Segoe UI Semibold", ScaleF(15), gp::FontStyleRegular, gp::UnitPixel);
  gp::StringFormat format;
  format.SetAlignment(gp::StringAlignmentCenter);
  format.SetLineAlignment(gp::StringAlignmentCenter);
  format.SetTrimming(gp::StringTrimmingEllipsisWord);
  gp::SolidBrush white(gp::Color(255, 255, 255, 255));
  gp::RectF text(caption.X + ScaleF(10), caption.Y, caption.Width - ScaleF(20), caption.Height);
  g.DrawString(game.info->title, -1, &font, text, &format, &white);
  if (game.location_gone) {
    gp::SolidBrush faded(gp::Color(150, 255, 255, 255));
    g.FillRectangle(&faded, r);
  }
  if (hover) {
    gp::SolidBrush light(gp::Color(28, 255, 255, 255));
    g.FillRectangle(&light, r);
  }
  g.ResetClip();
}

void DrawAddTile(gp::Graphics& g, const gp::RectF& r, bool hover) {
  gp::GraphicsPath tile;
  RoundedRect(tile, r, ScaleF(12));
  gp::SolidBrush background(hover ? gp::Color(255, 236, 240, 247) : gp::Color(255, 245, 246, 248));
  g.FillPath(&background, &tile);
  gp::Pen border(gp::Color(255, 160, 166, 178), ScaleF(2));
  border.SetDashStyle(gp::DashStyleDash);
  g.DrawPath(&border, &tile);
  gp::Pen plus(gp::Color(255, 110, 118, 132), ScaleF(5));
  plus.SetStartCap(gp::LineCapRound);
  plus.SetEndCap(gp::LineCapRound);
  float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2 - ScaleF(18), arm = ScaleF(22);
  g.DrawLine(&plus, cx - arm, cy, cx + arm, cy);
  g.DrawLine(&plus, cx, cy - arm, cx, cy + arm);
  gp::Font font(L"Segoe UI Semibold", ScaleF(15), gp::FontStyleRegular, gp::UnitPixel);
  gp::StringFormat format;
  format.SetAlignment(gp::StringAlignmentCenter);
  gp::SolidBrush text_brush(gp::Color(255, 70, 76, 88));
  std::wstring label = Tr(kTextAddTile);
  g.DrawString(label.c_str(), -1, &font, gp::RectF(r.X, cy + arm + ScaleF(14), r.Width, ScaleF(30)), &format,
               &text_brush);
}

void PaintGallery(HWND hwnd) {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(hwnd, &paint);
  RECT client{};
  GetClientRect(hwnd, &client);
  int w = std::max<int>(1, client.right), h = std::max<int>(1, client.bottom);
  gp::Bitmap buffer(w, h, PixelFormat32bppPARGB);
  {
    gp::Graphics g(&buffer);
    g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(gp::TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(gp::PixelOffsetModeHighQuality);
    g.Clear(gp::Color(255, 255, 255, 255));
    GalleryMetrics m = Metrics();
    bool focused = GetFocus() == hwnd;
    for (int t = 0; t < int(g_tiles.size()); ++t) {
      RECT tr = TileRect(t, m);
      if (tr.bottom < 0 || tr.top > h) continue;
      gp::RectF r(float(tr.left), float(tr.top), float(tr.right - tr.left), float(tr.bottom - tr.top));
      if (g_tiles[size_t(t)] < 0)
        DrawAddTile(g, r, t == g_hover);
      else
        DrawGameTile(g, r, g_games[size_t(g_tiles[size_t(t)])], t == g_hover);
      if (t == g_selected) {
        float inset = ScaleF(5);
        gp::GraphicsPath outline;
        RoundedRect(outline, gp::RectF(r.X - inset, r.Y - inset, r.Width + 2 * inset, r.Height + 2 * inset),
                    ScaleF(16));
        gp::Pen pen(focused ? gp::Color(255, 0, 103, 192) : gp::Color(255, 120, 150, 190), ScaleF(3));
        g.DrawPath(&pen, &outline);
      }
    }
  }
  gp::Graphics screen(dc);
  screen.DrawImage(&buffer, 0, 0);
  EndPaint(hwnd, &paint);
}

// ---- window -----------------------------------------------------------------------------------

void Layout();
bool ShowTerms(bool require_acceptance);
void OnPlay();
void OnAdd(HWND anchor, POINT at);
void ShowTileMenu(int tile, POINT at);

void SelectTile(int tile) {
  if (tile < 0 || tile >= int(g_tiles.size())) return;
  g_selected = tile;
  EnsureVisible(tile);
  InvalidateRect(g_gallery, nullptr, FALSE);
  void UpdateUi();
  UpdateUi();
}

const Game* SelectedGamePtr() {
  int index = SelectedGame();
  return index >= 0 ? &g_games[size_t(index)] : nullptr;
}

void UpdateUi() {
  const Game* game = SelectedGamePtr();
  bool playable = game && !game->location_gone && g_state == State::kIdle;
  std::wstring status;
  bool status_error = false;
  if (g_state == State::kWorking) {
    std::wstring title = g_job_game ? g_job_game->title : L"";
    status = g_percent < 0 || !g_job_game ? Tr(kTextChecking)
             : g_percent >= 100           ? Tr(kTextFinishing)
                                          : Tr(kTextExtracting, {title, std::to_wstring(g_percent)});
  } else if (!game) {
    status = Tr(kTextAddHint, {JoinTitles(AvailableGames())});
  } else if (game->location_gone) {
    status = Tr(kTextErrorLocationGone, {game->info->title, ShortPath(game->dir)});
    status_error = true;
  } else {
    status = Tr(game->info->webcam ? kTextReadyWebcam : kTextReadyPad, {game->info->title});
  }
  std::vector<const CatalogGame*> others;
  for (const Game& g : g_games)
    if (!g.installed) others.push_back(g.info);
  SetDlgItemTextW(g_window, kSupported, others.empty() ? L"" : Tr(kTextAlsoSupported, {JoinTitles(others)}).c_str());
  SetDlgItemTextW(g_window, kMessage, g_message.Render().c_str());
  SetDlgItemTextW(g_window, kStatus, status.c_str());
  SetWindowLongPtrW(GetDlgItem(g_window, kStatus), GWLP_USERDATA, status_error);
  ShowWindow(GetDlgItem(g_window, kMessage), g_message.id == kTextCount ? SW_HIDE : SW_SHOW);
  HWND progress = GetDlgItem(g_window, kProgress);
  ShowWindow(progress, g_state == State::kWorking ? SW_SHOW : SW_HIDE);
  if (g_percent < 0) {
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) | PBS_MARQUEE);
    SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
  } else {
    SendMessageW(progress, PBM_SETMARQUEE, FALSE, 0);
    SetWindowLongPtrW(progress, GWL_STYLE, GetWindowLongPtrW(progress, GWL_STYLE) & ~LONG_PTR(PBS_MARQUEE));
    SendMessageW(progress, PBM_SETPOS, g_percent, 0);
  }
  EnableWindow(GetDlgItem(g_window, kPlay), playable);
  SetDlgItemTextW(g_window, kAdd, Tr(g_state == State::kWorking ? kTextCancel : kTextAddGame).c_str());
  EnableWindow(GetDlgItem(g_window, kAdd), g_state != State::kRunning);
  fs::path logs = game ? LogDir(*game->info) : Root() / L"logs";
  EnableWindow(GetDlgItem(g_window, kLogs), Exists(logs));
  SendMessageW(g_window, DM_SETDEFID, playable ? kPlay : kAdd, 0);
  InvalidateRect(GetDlgItem(g_window, kStatus), nullptr, TRUE);
  InvalidateRect(g_gallery, nullptr, FALSE);
}

// The texts that don't depend on the state, after a language change.
void ApplyLanguage() {
  SetDlgItemTextW(g_window, kSubtitle, Tr(kTextSubtitle, {kVersion}).c_str());
  SetDlgItemTextW(g_window, kPlay, Tr(kTextPlay).c_str());
  SetDlgItemTextW(g_window, kLogs, Tr(kTextLogs).c_str());
  SetDlgItemTextW(g_window, kFooter, Tr(kTextFooter).c_str());
  UpdateUi();
}

fs::path ChooseFile(const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& filters,
                    bool folder) {
  fs::path chosen;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return chosen;
  std::vector<COMDLG_FILTERSPEC> types;
  for (const auto& [name, spec] : filters) types.push_back({name.c_str(), spec.c_str()});
  if (!types.empty()) dialog->SetFileTypes(UINT(types.size()), types.data());
  dialog->SetTitle(title.c_str());
  dialog->SetOptions(folder ? FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM
                            : FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
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

void StartJob(std::thread worker) {
  g_state = State::kWorking;
  g_percent = -1;
  g_message = {};
  g_job_game = nullptr;
  g_cancel = false;
  std::error_code ec;
  fs::create_directories(GamesDir(), ec);
  g_worker = std::move(worker);
  UpdateUi();
}

// `from_folder`: the player's folder, used in place; otherwise an archive (.7z, .rar or .zip) extracted here.
void StartAdding(const fs::path& source, bool from_folder) {
  StartJob(from_folder ? std::thread(AddFolderWorker, source) : std::thread(AddArchiveWorker, source));
}

void CancelJob() {
  g_cancel = true;
  std::lock_guard lock(g_extractor_mutex);
  if (g_extractor) TerminateProcess(g_extractor, 1);
}

// "Add game": where the copy comes from, in a menu at `at` (screen coordinates).
void OnAdd(HWND anchor, POINT at) {
  if (g_state == State::kWorking) return CancelJob();
  if (g_state != State::kIdle) return;
  std::wstring from_archive = Tr(kTextMenuArchive), from_folder = Tr(kTextMenuFolder);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuArchive, from_archive.c_str());
  AppendMenuW(menu, MF_STRING, kMenuFolder, from_folder.c_str());
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, anchor,
                              nullptr);
  DestroyMenu(menu);
  if (choice == kMenuArchive) {
    if (SevenZip().empty()) {
      MessageBoxW(g_window, Tr(kTextMissing7Zip).c_str(), L"AnyX360", MB_ICONERROR);
      return;
    }
    fs::path archive = ChooseFile(Tr(kTextDialogTitle),
                                  {{Tr(kTextFilterArchive), L"*.7z;*.rar;*.zip"}, {Tr(kTextFilterAll), L"*.*"}}, false);
    if (!archive.empty()) StartAdding(archive, false);
  } else if (choice == kMenuFolder) {
    fs::path folder = ChooseFile(Tr(kTextFolderTitle), {}, true);
    if (!folder.empty()) StartAdding(folder, true);
  }
}

void OnAddButton() {
  RECT button;
  GetWindowRect(GetDlgItem(g_window, kAdd), &button);
  OnAdd(g_window, {button.left, button.bottom});
}

void OnPlay() {
  const Game* game = SelectedGamePtr();
  if (!game || game->location_gone || g_state != State::kIdle) return;
  Message error = StartGame(*game);
  if (error.id != kTextCount) {
    g_message = error;
    UpdateUi();
    return;
  }
  g_state = State::kRunning;
  g_running = game->info;
  g_message = {};
  ShowWindow(g_window, SW_HIDE);
}

void ChangeCover(const Game& game) {
  fs::path image = ChooseFile(Tr(kTextCoverTitle, {game.info->title}),
                              {{Tr(kTextFilterImages), L"*.png;*.jpg;*.jpeg"}, {Tr(kTextFilterAll), L"*.*"}}, false);
  if (image.empty()) return;
  if (!LoadImage(image)) {
    g_message = {kTextErrorCover, {}, true};
    UpdateUi();
    return;
  }
  std::wstring extension = image.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
  fs::path target = GameRoot(*game.info) / (extension == L".png" ? L"cover.png" : L"cover.jpg");
  std::error_code ec;
  for (const wchar_t* name : {L"cover.png", L"cover.jpg"}) fs::remove(GameRoot(*game.info) / name, ec);
  fs::copy_file(image, target, fs::copy_options::overwrite_existing, ec);
  g_message = {};
  LoadLibrary();
  UpdateUi();
}

void ShowTileMenu(int tile, POINT at) {
  if (tile < 0 || tile >= int(g_tiles.size())) return;
  SelectTile(tile);
  if (g_tiles[size_t(tile)] < 0) return OnAdd(g_gallery, at);
  if (g_state != State::kIdle) return;
  const Game& game = g_games[size_t(g_tiles[size_t(tile)])];
  std::vector<std::pair<int, std::wstring>> items = {
      {kMenuPlay, Tr(kTextMenuPlay)},
      {0, L""},
      {kMenuCover, Tr(kTextMenuCover)},
      {kMenuRestoreCover, Tr(kTextMenuRestoreCover)},
      {kMenuLogs, Tr(kTextMenuLogs)},
      {0, L""},
      {kMenuPrepareAgain, Tr(kTextMenuPrepareAgain)},
      {kMenuRemove, Tr(kTextMenuRemove)},
  };
  HMENU menu = CreatePopupMenu();
  for (const auto& [id, text] : items) {
    UINT flags = id ? MF_STRING : MF_SEPARATOR;
    if ((id == kMenuPlay && game.location_gone) || (id == kMenuRestoreCover && !game.cover) ||
        (id == kMenuLogs && !Exists(LogDir(*game.info))))
      flags |= MF_GRAYED;
    AppendMenuW(menu, flags, UINT_PTR(id), text.c_str());
  }
  if (!game.location_gone) SetMenuDefaultItem(menu, kMenuPlay, FALSE);
  int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0,
                              g_gallery, nullptr);
  DestroyMenu(menu);
  std::error_code ec;
  switch (choice) {
    case kMenuPlay: OnPlay(); break;
    case kMenuCover: ChangeCover(game); break;
    case kMenuRestoreCover:
      for (const wchar_t* name : {L"cover.png", L"cover.jpg"}) fs::remove(GameRoot(*game.info) / name, ec);
      LoadLibrary();
      UpdateUi();
      break;
    case kMenuLogs: ShellExecuteW(g_window, L"open", LogDir(*game.info).c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case kMenuPrepareAgain: OnAdd(g_gallery, at); break;
    case kMenuRemove:
      if (MessageBoxW(g_window, Tr(kTextConfirmRemove, {game.info->title}).c_str(), L"AnyX360",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        StartJob(std::thread(RemoveWorker, game.info));
        g_job_game = game.info;
        UpdateUi();
      }
      break;
  }
}

LRESULT CALLBACK GalleryProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: PaintGallery(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      ClampScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONDOWN: {
      SetFocus(hwnd);
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile >= 0) SelectTile(tile);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile < 0) return 0;
      SelectTile(tile);
      if (g_tiles[size_t(tile)] < 0) {
        POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ClientToScreen(hwnd, &at);
        OnAdd(hwnd, at);
      } else {
        OnPlay();
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      int tile = TileAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (tile != g_hover) {
        g_hover = tile;
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
      }
      SetCursor(LoadCursorW(nullptr, tile >= 0 ? IDC_HAND : IDC_ARROW));
      return 0;
    }
    case WM_MOUSELEAVE:
      g_hover = -1;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      g_scroll -= GET_WHEEL_DELTA_WPARAM(wparam) * Scale(120) / WHEEL_DELTA;
      ClampScroll();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_CONTEXTMENU: {
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      int tile = g_selected;
      if (at.x == -1 && at.y == -1) {  // the keyboard's menu key: under the selected tile
        RECT r = TileRect(tile, Metrics());
        at = {r.left + Scale(16), r.top + Scale(16)};
        ClientToScreen(hwnd, &at);
      } else {
        POINT client = at;
        ScreenToClient(hwnd, &client);
        tile = TileAt(client.x, client.y);
      }
      ShowTileMenu(tile, at);
      return 0;
    }
    case WM_KEYDOWN: {
      int columns = Metrics().columns;
      int tile = g_selected;
      switch (wparam) {
        case VK_LEFT: tile -= 1; break;
        case VK_RIGHT: tile += 1; break;
        case VK_UP: tile -= columns; break;
        case VK_DOWN: tile += columns; break;
        case VK_HOME: tile = 0; break;
        case VK_END: tile = int(g_tiles.size()) - 1; break;
        default: return DefWindowProcW(hwnd, message, wparam, lparam);
      }
      SelectTile(std::clamp(tile, 0, int(g_tiles.size()) - 1));
      return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(hwnd, nullptr, FALSE); return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void CreateFonts(UINT dpi) {
  for (HFONT font : {g_title_font, g_text_font, g_small_font})
    if (font) DeleteObject(font);
  auto make = [dpi](int points, int weight) {
    return CreateFontW(-MulDiv(points, int(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  };
  g_title_font = make(20, FW_SEMIBOLD);
  g_text_font = make(10, FW_NORMAL);
  g_small_font = make(9, FW_NORMAL);
  for (int id : {kTitle, kSubtitle, kSupported, kStatus, kMessage, kPlay, kAdd, kLogs, kFooter}) {
    HFONT font = id == kTitle ? g_title_font : id == kSubtitle || id == kFooter || id == kSupported ? g_small_font
                                                                                                      : g_text_font;
    SendDlgItemMessageW(g_window, id, WM_SETFONT, WPARAM(font), TRUE);
  }
}

constexpr int kClientWidth = 860, kClientHeight = 640, kMinWidth = 560, kMinHeight = 520;

void Layout() {
  RECT client{};
  GetClientRect(g_window, &client);
  int dpi = int(GetDpiForWindow(g_window));
  int w = MulDiv(client.right, 96, dpi), h = MulDiv(client.bottom, 96, dpi);  // in 96-DPI units
  int inner = w - 48;
  struct Item {
    int id, x, y, w, h;
  };
  const Item items[] = {
      {kTitle, 24, 14, inner, 40},
      {kSubtitle, 26, 54, inner, 20},
      {kGallery, 12, 82, w - 24, h - 82 - 212},
      {kSupported, 24, h - 204, inner, 20},
      {kMessage, 24, h - 182, inner, 40},
      {kStatus, 24, h - 138, inner, 40},
      {kProgress, 24, h - 94, inner, 14},
      {kPlay, 24, h - 72, 150, 36},
      {kAdd, 186, h - 72, 190, 36},
      {kLogs, w - 24 - 128, h - 72, 128, 36},
      {kFooter, 24, h - 28, inner, 20},
  };
  for (const Item& item : items)
    SetWindowPos(GetDlgItem(g_window, item.id), nullptr, Scale(item.x), Scale(item.y), Scale(std::max(item.w, 1)),
                 Scale(std::max(item.h, 1)), SWP_NOZORDER | SWP_NOACTIVATE);
  ClampScroll();
}

void CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g_window, HMENU(INT_PTR(id)),
                           instance, nullptr);
  };
  add(L"STATIC", L"AnyX360", SS_LEFT | SS_NOPREFIX, kTitle);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kSubtitle);
  g_gallery = add(L"AnyX360Gallery", L"", WS_TABSTOP, kGallery);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kSupported);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kMessage);
  add(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, kStatus);
  add(PROGRESS_CLASSW, L"", 0, kProgress);
  add(L"BUTTON", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, kPlay);
  add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, kAdd);
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
  std::wstring title = Tr(kTextTermsTitle);
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
  if (require_acceptance && g_terms.accepted) WriteSetting(SettingsFile(L"terms_accepted.txt"), TermsHash());
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
        case kAdd: OnAddButton(); return 0;
        case kLogs: {
          const Game* game = SelectedGamePtr();
          fs::path logs = game && Exists(LogDir(*game->info)) ? LogDir(*game->info) : Root() / L"logs";
          ShellExecuteW(hwnd, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          return 0;
        }
      }
      break;
    case kMsgProgress:
      g_percent = int(wparam);
      UpdateUi();
      return 0;
    case kMsgJobGame:
      g_job_game = reinterpret_cast<const CatalogGame*>(lparam);
      UpdateUi();
      return 0;
    case kMsgAskReplace: {
      const CatalogGame* game = reinterpret_cast<const CatalogGame*>(lparam);
      return MessageBoxW(hwnd, Tr(kTextConfirmReplace, {game->title}).c_str(), L"AnyX360",
                         MB_YESNO | MB_ICONQUESTION);
    }
    case kMsgDone: {
      if (g_worker.joinable()) g_worker.join();
      std::unique_ptr<Outcome> outcome(reinterpret_cast<Outcome*>(lparam));
      g_state = State::kIdle;
      g_message = outcome->message.id != kTextCount ? outcome->message
                                                    : Message{g_cancel ? kTextCancelled : kTextCount, {}, false};
      g_cancel = false;
      g_job_game = nullptr;
      if (g_close_after_cancel) {
        DestroyWindow(hwnd);
        return 0;
      }
      LoadLibrary(outcome->select);
      EnsureVisible(g_selected);
      UpdateUi();
      return 0;
    }
    case kMsgIconReady:
      if (g_state == State::kIdle) {
        LoadLibrary();
        UpdateUi();
      }
      return 0;
    case kMsgGameExited: {
      if (wparam == 0) {  // closed normally (or restarted from the game's settings panel)
        DestroyWindow(hwnd);
        return 0;
      }
      g_state = State::kIdle;
      wchar_t code[16];
      swprintf(code, 16, DWORD(wparam) > 255 ? L"0x%08X" : L"%u", DWORD(wparam));
      g_message = {kTextErrorExited, {g_running ? g_running->title : L"", code}, true};
      g_running = nullptr;
      UpdateUi();
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = HDC(wparam);
      HWND control = HWND(lparam);
      int id = GetDlgCtrlID(control);
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, id == kSubtitle || id == kFooter || id == kSupported ? RGB(110, 110, 110)
                       : id == kMessage && g_message.error                 ? RGB(170, 30, 30)
                       : id == kMessage                                    ? RGB(20, 110, 40)
                       : id == kStatus && GetWindowLongPtrW(control, GWLP_USERDATA) ? RGB(170, 30, 30)
                                                                                     : RGB(25, 25, 25));
      return LRESULT(GetStockObject(WHITE_BRUSH));
    }
    case WM_SIZE:
      Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      if (!g_window) break;
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
      RECT frame{0, 0, Scale(kMinWidth), Scale(kMinHeight)};
      AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, GetDpiForWindow(hwnd));
      info->ptMinTrackSize = {frame.right - frame.left, frame.bottom - frame.top};
      return 0;
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
      if (g_state == State::kWorking) {
        if (MessageBoxW(hwnd, Tr(kTextConfirmCancelExit).c_str(), L"AnyX360", MB_YESNO | MB_ICONQUESTION) != IDYES)
          return 0;
        g_close_after_cancel = true;
        CancelJob();  // the window closes when the worker has cleaned up
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
  gp::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus = 0;
  gp::GdiplusStartup(&gdiplus, &gdiplus_input, nullptr);
  LoadLanguage();

  WNDCLASSEXW gallery{sizeof(gallery)};
  gallery.style = CS_DBLCLKS;
  gallery.lpfnWndProc = GalleryProc;
  gallery.hInstance = instance;
  gallery.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  gallery.lpszClassName = L"AnyX360Gallery";
  RegisterClassExW(&gallery);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = HBRUSH(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = L"AnyX360Launcher";
  RegisterClassExW(&wc);
  const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
  g_window = CreateWindowExW(0, wc.lpszClassName, L"AnyX360", style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr,
                             nullptr, instance, nullptr);
  if (!g_window) return 1;

  // Size the client area for the window's DPI and center it on its monitor.
  UINT dpi = GetDpiForWindow(g_window);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor);
  RECT frame{0, 0, Scale(kClientWidth), Scale(kClientHeight)};
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
  int width = std::min<int>(frame.right - frame.left, monitor.rcWork.right - monitor.rcWork.left);
  int height = std::min<int>(frame.bottom - frame.top, monitor.rcWork.bottom - monitor.rcWork.top);
  CreateControls();
  CreateFonts(dpi);
  SetWindowPos(g_window, nullptr, (monitor.rcWork.left + monitor.rcWork.right - width) / 2,
               (monitor.rcWork.top + monitor.rcWork.bottom - height) / 2, width, height, SWP_NOZORDER);
  std::wstring select;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i + 1 < argc; ++i)
    if (std::wstring(argv[i]) == L"--select") select = argv[i + 1];
  LoadLibrary(select);
  Layout();
  ApplyLanguage();
  if (!TermsAccepted() && !ShowTerms(true)) {
    DestroyWindow(g_window);
    gp::GdiplusShutdown(gdiplus);
    CoUninitialize();
    return 0;
  }
  ShowWindow(g_window, show);
  SetFocus(g_gallery);
  for (int i = 1; i + 1 < argc && g_state == State::kIdle; ++i) {  // one job: the first one given
    std::wstring arg = argv[i];
    if (arg == L"--archive" && !SevenZip().empty()) StartAdding(argv[i + 1], false);
    if (arg == L"--folder") StartAdding(argv[i + 1], true);
  }
  LocalFree(argv);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g_window, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g_worker.joinable()) g_worker.join();
  g_games.clear();  // the bitmaps go before GDI+ does
  gp::GdiplusShutdown(gdiplus);
  CoUninitialize();
  return 0;
}
