#include "core/strings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <iterator>

#include "core/paths.h"
#include "games_catalog.h"

namespace anyx360::launcher {
namespace {

Language g_language = Language::kEnglish;

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
    L"{0} is ready, with its downloadable content ({1}).",
    L"The Games on Demand copy could not be read ({0}).",
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
    L"{0} está preparado, con su contenido descargable ({1}).",
    L"No se pudo leer la copia Games on Demand ({0}).",
    L"Términos y condiciones de uso (AnyX360)",
    L"Términos y condiciones de uso",
    L"He leído y acepto los términos y condiciones de uso",
    L"Aceptar",
    L"No acepto",
    L"Cerrar",
};
static_assert(std::size(kEnglish) == kTextCount && std::size(kSpanish) == kTextCount);

}  // namespace

Language CurrentLanguage() { return g_language; }
void SetLanguage(Language language) { g_language = language; }

void LoadLanguage() {
  std::wstring saved = ReadSetting(SettingsFile(L"launcher_language.txt"));
  if (saved == L"es" || saved == L"en")
    g_language = saved == L"es" ? Language::kSpanish : Language::kEnglish;
  else
    g_language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH ? Language::kSpanish : Language::kEnglish;
}

void SaveLanguage(Language language) {
  g_language = language;
  WriteSetting(SettingsFile(L"launcher_language.txt"), language == Language::kSpanish ? L"es" : L"en");
}

std::wstring Format(std::wstring text, const std::vector<std::wstring>& args) {
  for (size_t i = 0; i < args.size(); ++i) {
    std::wstring marker = L"{" + std::to_wstring(i) + L"}";
    for (size_t at = 0; (at = text.find(marker, at)) != std::wstring::npos; at += args[i].size())
      text.replace(at, marker.size(), args[i]);
  }
  return text;
}

std::wstring Tr(Text id, const std::vector<std::wstring>& args) {
  return Format((g_language == Language::kSpanish ? kSpanish : kEnglish)[id], args);
}

std::wstring JoinTitles(const std::vector<const CatalogGame*>& games) {
  std::wstring text;
  for (size_t i = 0; i < games.size(); ++i) {
    if (i) text += i + 1 == games.size() ? (g_language == Language::kSpanish ? L" y " : L" and ") : L", ";
    text += games[i]->title;
  }
  return text;
}

std::wstring Widen(const std::string& utf8) {
  int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  std::wstring text(size_t(std::max(length, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), text.data(), length);
  return text;
}

std::string Narrow(const std::wstring& text) {
  int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string utf8(size_t(std::max(length, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), length, nullptr, nullptr);
  return utf8;
}

std::wstring ShortPath(const std::filesystem::path& path) {
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

}  // namespace anyx360::launcher
