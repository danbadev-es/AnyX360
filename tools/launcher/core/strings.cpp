#include "core/strings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "core/paths.h"
#include "games_catalog.h"

namespace anyx360::launcher {
namespace {

Language g_language = Language::kEnglish;

const wchar_t* const kEnglish[] = {
    L"Checking the game…",
    L"Finishing…",
    L"Extracting {0}… {1} %",
    L"Removing {0}…",
    L"Play",
    L"Add game",
    L"Cancel",
    L"Change cover…",
    L"Restore the game's cover",
    L"Open logs",
    L"Prepare again…",
    L"Remove from the library…",
    L"Choose your game: an archive, a disc image or a package",
    L"Choose the folder of your game (the one with default.xex)",
    L"Choose an image for {0}",
    L"Games (all supported formats)",
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
    L"7-Zip could not read that file (code {0}). Check that it is your game's archive and that it is complete.",
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
    L"The copy could not be read ({0}).",
    L"Terms of use",
    L"I have read and accept the terms of use",
    L"Accept",
    L"Decline",
    L"Close",
    L"{0} is protected by a password. Enter it to open it:",
    L"That archive is protected by a password.",
    L"That password doesn't open the archive.",
    L"A part of {0} is missing: keep all its parts in the same folder and choose the first one.",
    L"AnyX360 doesn't recognise that file as a game: choose your game's archive (.7z, .rar, .zip...), disc image "
    L"(.iso), Games on Demand or Xbox Live Arcade package, AnyX360 backup (.anyx), or the folder where it is.",
    L"That is downloadable content for {0}, not the game. Add the game first; its content can be added after.",
    L"That is a title update of a game AnyX360 plays in its own version, without the update: add the game "
    L"instead.",
    L"That is downloadable content for {0}, which this release doesn't play.",
    L"{1} downloadable content packs were added to {0}.",
    L"{0} title updates in the copy were left out (AnyX360 plays the game's own version).",
    L"{0} packages of other games in the copy were left out.",
    L"{0} downloadable content packs in the copy were left out: this game's need its title update, which "
    L"AnyX360 doesn't play.",
    L"my games",
    L"compatible games",
    L"{0} playable · {1} coming soon",
    L"Playable",
    L"In testing",
    L"In your library",
    L"Coming soon",
    L"Add your first game",
    L"Your copy as you have it: an archive (.7z, .rar, .zip…), a disc image (.iso), a Games on Demand or Xbox Live Arcade package, an AnyX360 backup (.anyx), or the folder with its files. This release plays {0}.",
    L"add a game",
    L"From a file",
    L"An archive (.7z, .rar, .zip…), a disc image (.iso), a package or an .anyx backup",
    L"From a folder",
    L"The folder with your game's files: it is played from there",
    L"You can also drop your game on this window.",
    L"Drop it to add the game",
    L"adding {0}",
    L"checking your game",
    L"removing {0}",
    L"password",
    L"Type it with the keyboard and press Enter.",
    L"OK",
    L"Replace",
    L"Keep the one I have",
    L"Remove",
    L"Exit",
    L"Keep going",
    L"Open logs",
    L"that didn't work",
    L"the game closed",
    L"already in your library",
    L"remove the game",
    L"exit",
    L"Español",
    L"Terms of use",
    L"Developed by danbadev-es",
    L"select",
    L"back",
    L"more",
    L"compatible games",
    L"Played from {0}",
    L"Prepared in the games folder",
    L"Keyboard and mouse or an Xbox controller",
    L"Played with your webcam: your whole body in view, 2-3 m away",
    L"Profiles",
    L"Profiles",
    L"Each player's profile, as on the console: its gamertag, its saves in every game and its time played. "
    L"The games are told the language of the profile playing; a game without it uses its own.",
    L"Player {0}",
    L"Letters, numbers and spaces, up to 15.",
    L"Game language",
    L"Automatic: {0}, as Windows",
    L"Automatic: English (Windows' language isn't one of the console's)",
    L"Save",
    L"Save as .anyx…",
    L"Save {0} as an .anyx backup",
    L"AnyX360 backups (.anyx)",
    L"Saving {0}… {1} %",
    L"saving {0}",
    L"Checking {0}'s files… {1} %",
    L"{0} was saved to {1} ({2}): a backup of your copy with its downloadable content (your saves stay "
    L"where they are). Add game takes it back, on this PC or another of yours. Don't give it to anyone: "
    L"each player uses their own copy.",
    L"That copy of {0} is version {1}; AnyX360 plays version {2}, the one it was recompiled from. Add a "
    L"copy of that version.",
    L"That .anyx file was saved by a newer AnyX360 ({0}). Update AnyX360 to add it.",
    L"That .anyx file is damaged or was changed ({0}). Save it again from your copy.",
    L"Choose a place outside AnyX360's games folder and outside the game's own folder.",
    L"The .anyx file could not be written (7-Zip code {0}). Check the free space and the folder.",
    L"AnyX360 can't save the copy in {0} from where it is (a network folder?). Copy that folder to this PC "
    L"first, or prepare the game again.",
    L"Not enough space on {2}: {0} needed, {1} free.",
    L"{0} can't hold files over 4 GB (FAT32). Choose another drive, or one formatted as exFAT or NTFS.",
    L"{0} isn't in the library: add it first.",
    L"{0} is played with its title update ({1}), which hasn't been added. Add it with Add game: the archive "
    L"the game came in, or the update's TU_ file or package.",
    L"{0}'s title update ({1}) was added: the game is ready.",
    L"Its title update ({0}) was added too.",
    L"A title update for version {0} was left out: AnyX360 plays version {1}.",
    L"That title update makes {0} version {1}; AnyX360 plays version {2}, the one it was recompiled from. "
    L"Add the update of that version.",
    L"A title update couldn't be read: its package is damaged.",
    L"That is {0}'s title update. Add the game first; its update can be added after.",
    L"Needs its title update",
    L"Who's playing?",
    L"Choose your profile: it keeps its own saves in every game. The other players choose theirs in the "
    L"game's sign-in screen.",
    L"Create profile",
    L"Switch profile",
    L"New profile",
    L"Rename {0}",
    L"Create",
    L"Play as {0}",
    L"Rename…",
    L"Delete profile…",
    L"Delete {0}?",
    L"{0} is no longer a profile here. Its saves stay in each game's folder (players\\{1}, or the game's "
    L"own folder), and its time played stays too: nothing is erased.",
    L"Delete",
    L"Type a gamertag.",
    L"Another profile is called {0}.",
    L"The profile couldn't be saved in {0}.",
    L"Playing",
    L"Space",
    L"Shift",
    L"Delete",
    L"{0} has played {1} · last on {2}",
    L"{0} hasn't played it yet",
    L"{0} h {1} min",
    L"{0} min",
    L"under a minute",
    L"Shift",
    L"Space",
    L"Done",
    L"Achievements",
    L"Achievements  {0}/{1} G",
    L"{0}: {1} of {2} unlocked · {3} of {4} G",
    L"{0} achievements, {1} G. Choose who's playing to see what their profile has unlocked.",
    L"Reading the game's achievements…",
    L"This game has no achievements.",
    L"Secret achievement",
    L"Keep playing to unlock it.",
    L"Unlocked on {0}",
    L"{0} G",
    L"Console mode",
    L"Desktop mode",
    L"Home",
    L"Games",
    L"Profiles",
    L"Settings",
    L"Add a game",
    L"Last played",
    L"Recently played",
    L"Your games",
    L"No games yet: add one from your own copy.",
    L"{0} games played · {1}",
    L"Nobody is signed in",
    L"{0} of {1} G",
    L"{0} of {1} unlocked",
    L"Signed in",
    L"Manage profiles",
    L"Display: in this window",
    L"Display: full screen",
    L"Language: {0}",
    L"English",
    L"Each game's own settings are in the game: press the Xbox button (or hold Back and Start, or press Home) and choose Settings.",
    L"The launcher's window with the games in a row and the compatible games below.",
    L"Starting {0}…",
    L"Pages",
    L"Play",
    L"Avatar",
    L"{0}'s avatar",
    L"Reading the avatar pack…",
    L"There's no avatar pack yet. It comes with the avatar games (Kinect Sports, Kinect Joy Ride…): add one of them and come back.",
    L"Choose who's playing first: the avatar belongs to a profile.",
    L"The avatar can't be shown: {0}",
    L"Body",
    L"Male",
    L"Female",
    L"Hair",
    L"Hair colour",
    L"Eyes",
    L"Eye colour",
    L"Eyebrows",
    L"Mouth",
    L"Lip colour",
    L"Facial hair",
    L"Nose",
    L"Chin",
    L"Ears",
    L"Skin colour",
    L"Height",
    L"Weight",
    L"Top",
    L"Bottom",
    L"Shoes",
    L"Hat",
    L"Glasses",
    L"Accessories",
    L"Colours",
    L"Its own",
    L"{0} of {1}",
    L"None",
    L"The head's own",
    L"Random avatar",
    L"Gamerpic…",
    L"{0}'s avatar is saved: the games show it from now on.",
    L"Leave the avatar?",
    L"The changes aren't saved.",
    L"Discard",
    L"Right stick or drag: turn it · up and down on the stick, or the wheel: zoom to the face",
    L"Edit avatar…",
    L"Change gamerpic…",
    L"{0}'s gamerpic",
    L"The profile's picture in the dashboard and in the games' Guide: its avatar, or a picture from one of your games.",
    L"Avatar",
    L"Initial",
    L"{0}'s gamerpic is changed.",
    L"The avatar or the gamerpic can't be saved in {0}.",
    L"Turn",
    L"Change",
};

const wchar_t* const kSpanish[] = {
    L"Comprobando el juego…",
    L"Terminando…",
    L"Extrayendo {0}… {1} %",
    L"Quitando {0}…",
    L"Jugar",
    L"Añadir juego",
    L"Cancelar",
    L"Cambiar portada…",
    L"Recuperar la portada del juego",
    L"Abrir registros",
    L"Volver a preparar…",
    L"Quitar de la biblioteca…",
    L"Elige tu juego: un archivo comprimido, una imagen de disco o un paquete",
    L"Elige la carpeta de tu juego (la que tiene default.xex)",
    L"Elige una imagen para {0}",
    L"Juegos (todos los formatos compatibles)",
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
    L"7-Zip no pudo leer ese archivo (código {0}). Comprueba que es el archivo de tu juego y que está completo.",
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
    L"No se pudo leer la copia ({0}).",
    L"Términos y condiciones de uso",
    L"He leído y acepto los términos y condiciones de uso",
    L"Aceptar",
    L"No acepto",
    L"Cerrar",
    L"{0} está protegido con contraseña. Escríbela para abrirlo:",
    L"Ese archivo está protegido con contraseña.",
    L"Esa contraseña no abre el archivo.",
    L"Falta una parte de {0}: deja todas sus partes en la misma carpeta y elige la primera.",
    L"AnyX360 no reconoce ese archivo como un juego: elige el archivo comprimido de tu juego (.7z, .rar, .zip...), "
    L"su imagen de disco (.iso), su paquete Games on Demand o de Xbox Live Arcade, su copia de seguridad de AnyX360 "
    L"(.anyx), o la carpeta donde está.",
    L"Eso es contenido descargable de {0}, no el juego. Añade antes el juego; su contenido se puede añadir después.",
    L"Eso es una actualización del título de un juego al que AnyX360 juega en su propia versión, sin la "
    L"actualización: añade el juego.",
    L"Eso es contenido descargable de {0}, que esta versión no juega.",
    L"Se han añadido {1} paquetes de contenido descargable a {0}.",
    L"Se han dejado fuera {0} actualizaciones del título que había en la copia (AnyX360 juega a la versión del "
    L"propio juego).",
    L"Se han dejado fuera {0} paquetes de otros juegos que había en la copia.",
    L"Se han dejado fuera {0} paquetes de contenido descargable que había en la copia: los de este juego "
    L"necesitan su actualización del título, que AnyX360 no juega.",
    L"mis juegos",
    L"juegos compatibles",
    L"{0} jugables · {1} próximamente",
    L"Jugable",
    L"En pruebas",
    L"En tu biblioteca",
    L"Próximamente",
    L"Añade tu primer juego",
    L"Tu copia tal como la tienes: un archivo comprimido (.7z, .rar, .zip…), una imagen de disco (.iso), un paquete Games on Demand o de Xbox Live Arcade, una copia de seguridad de AnyX360 (.anyx), o la carpeta con sus archivos. Esta versión juega a {0}.",
    L"añadir un juego",
    L"Desde un archivo",
    L"Un archivo comprimido (.7z, .rar, .zip…), una imagen de disco (.iso), un paquete o una copia .anyx",
    L"Desde una carpeta",
    L"La carpeta con los archivos de tu juego: se juega desde ahí",
    L"También puedes soltar tu juego sobre esta ventana.",
    L"Suéltalo para añadir el juego",
    L"añadiendo {0}",
    L"comprobando tu juego",
    L"quitando {0}",
    L"contraseña",
    L"Escríbela con el teclado y pulsa Intro.",
    L"Aceptar",
    L"Reemplazar",
    L"Conservar el que tengo",
    L"Quitar",
    L"Salir",
    L"Continuar",
    L"Abrir registros",
    L"no ha funcionado",
    L"el juego se ha cerrado",
    L"ya está en tu biblioteca",
    L"quitar el juego",
    L"salir",
    L"English",
    L"Términos de uso",
    L"Desarrollado por danbadev-es",
    L"elegir",
    L"atrás",
    L"más",
    L"juegos compatibles",
    L"Se juega desde {0}",
    L"Preparado en la carpeta games",
    L"Teclado y ratón o mando de Xbox",
    L"Se juega con tu webcam: todo el cuerpo a la vista, a 2-3 m",
    L"Perfiles",
    L"Perfiles",
    L"El perfil de cada jugador, como en la consola: su gamertag, sus partidas guardadas en cada juego y "
    L"su tiempo de juego. A los juegos se les dice el idioma del perfil que juega; un juego que no lo "
    L"tenga usa el suyo.",
    L"Jugador {0}",
    L"Letras, números y espacios, hasta 15.",
    L"Idioma de los juegos",
    L"Automático: {0}, como Windows",
    L"Automático: inglés (el idioma de Windows no es uno de los de la consola)",
    L"Guardar",
    L"Guardar como .anyx…",
    L"Guardar {0} como copia de seguridad .anyx",
    L"Copias de seguridad de AnyX360 (.anyx)",
    L"Guardando {0}… {1} %",
    L"guardando {0}",
    L"Comprobando los archivos de {0}… {1} %",
    L"{0} se ha guardado en {1} ({2}): una copia de seguridad de tu copia con su contenido descargable (tus "
    L"partidas guardadas se quedan donde están). Añadir juego la recupera, en este PC o en otro tuyo. No se la "
    L"des a nadie: cada jugador usa su propia copia.",
    L"Esa copia de {0} es la versión {1}; AnyX360 juega a la versión {2}, la que se recompiló. Añade una "
    L"copia de esa versión.",
    L"Ese archivo .anyx lo guardó un AnyX360 más nuevo ({0}). Actualiza AnyX360 para añadirlo.",
    L"Ese archivo .anyx está dañado o se ha modificado ({0}). Vuelve a guardarlo desde tu copia.",
    L"Elige un lugar fuera de la carpeta games de AnyX360 y fuera de la carpeta del propio juego.",
    L"No se pudo escribir el archivo .anyx (código de 7-Zip {0}). Comprueba el espacio libre y la carpeta.",
    L"AnyX360 no puede guardar la copia de {0} desde donde está (¿una carpeta de red?). Copia antes esa "
    L"carpeta a este PC, o vuelve a preparar el juego.",
    L"No hay espacio suficiente en {2}: hacen falta {0} y hay {1}.",
    L"{0} no admite archivos de más de 4 GB (FAT32). Elige otra unidad, o una con formato exFAT o NTFS.",
    L"{0} no está en la biblioteca: añádelo antes.",
    L"{0} se juega con su actualización del título ({1}), que no se ha añadido. Añádela con Añadir juego: el "
    L"archivo comprimido del que vino el juego, o el archivo TU_ o el paquete de la actualización.",
    L"Se ha añadido la actualización del título de {0} ({1}): el juego está listo.",
    L"También se ha añadido su actualización del título ({0}).",
    L"Se ha dejado fuera una actualización del título para la versión {0}: AnyX360 juega a la versión {1}.",
    L"Esa actualización del título deja {0} en la versión {1}; AnyX360 juega a la versión {2}, la que se "
    L"recompiló. Añade la actualización de esa versión.",
    L"No se ha podido leer una actualización del título: su paquete está dañado.",
    L"Esa es la actualización del título de {0}. Añade antes el juego; su actualización se puede añadir después.",
    L"Necesita su actualización del título",
    L"¿Quién juega?",
    L"Elige tu perfil: tiene sus propias partidas guardadas en cada juego. Los demás jugadores eligen el "
    L"suyo en la pantalla de inicio de sesión del juego.",
    L"Crear perfil",
    L"Cambiar de perfil",
    L"Perfil nuevo",
    L"Cambiar el nombre de {0}",
    L"Crear",
    L"Jugar como {0}",
    L"Cambiar el nombre…",
    L"Eliminar el perfil…",
    L"¿Eliminar a {0}?",
    L"{0} deja de ser un perfil aquí. Sus partidas guardadas se quedan en la carpeta de cada juego "
    L"(players\\{1}, o la propia carpeta del juego), y su tiempo de juego también: no se borra nada.",
    L"Eliminar",
    L"Escribe un gamertag.",
    L"Otro perfil se llama {0}.",
    L"No se ha podido guardar el perfil en {0}.",
    L"Jugando",
    L"Espacio",
    L"Mayús",
    L"Borrar",
    L"{0} ha jugado {1} · la última vez el {2}",
    L"{0} aún no lo ha jugado",
    L"{0} h {1} min",
    L"{0} min",
    L"menos de un minuto",
    L"Mayús",
    L"Espacio",
    L"Hecho",
    L"Logros",
    L"Logros  {0}/{1} G",
    L"{0}: {1} de {2} desbloqueados · {3} de {4} G",
    L"{0} logros, {1} G. Elige quién juega para ver lo que ha desbloqueado su perfil.",
    L"Leyendo los logros del juego…",
    L"Este juego no tiene logros.",
    L"Logro secreto",
    L"Sigue jugando para desbloquearlo.",
    L"Desbloqueado el {0}",
    L"{0} G",
    L"Modo consola",
    L"Modo escritorio",
    L"Inicio",
    L"Juegos",
    L"Perfiles",
    L"Ajustes",
    L"Añadir un juego",
    L"Lo último que has jugado",
    L"Jugados recientemente",
    L"Tus juegos",
    L"Aún no hay juegos: añade uno de tu propia copia.",
    L"{0} juegos jugados · {1}",
    L"Nadie ha iniciado sesión",
    L"{0} de {1} G",
    L"{0} de {1} desbloqueados",
    L"Sesión iniciada",
    L"Gestionar perfiles",
    L"Pantalla: en esta ventana",
    L"Pantalla: pantalla completa",
    L"Idioma: {0}",
    L"Español",
    L"Los ajustes de cada juego están en el juego: pulsa el botón Xbox (o mantén Back y Start, o pulsa Inicio) y elige Ajustes.",
    L"La ventana del lanzador con los juegos en fila y los juegos compatibles debajo.",
    L"Iniciando {0}…",
    L"Páginas",
    L"Jugar",
    L"Avatar",
    L"Avatar de {0}",
    L"Leyendo el paquete de avatares…",
    L"Aún no hay paquete de avatares. Viene con los juegos de avatares (Kinect Sports, Kinect Joy Ride…): añade uno de ellos y vuelve.",
    L"Elige primero quién juega: el avatar es de un perfil.",
    L"No se puede mostrar el avatar: {0}",
    L"Cuerpo",
    L"Hombre",
    L"Mujer",
    L"Pelo",
    L"Color del pelo",
    L"Ojos",
    L"Color de ojos",
    L"Cejas",
    L"Boca",
    L"Color de labios",
    L"Vello facial",
    L"Nariz",
    L"Barbilla",
    L"Orejas",
    L"Color de piel",
    L"Altura",
    L"Complexión",
    L"Parte de arriba",
    L"Parte de abajo",
    L"Calzado",
    L"Sombrero",
    L"Gafas",
    L"Accesorios",
    L"Colores",
    L"Los suyos",
    L"{0} de {1}",
    L"Ninguno",
    L"La de la cabeza",
    L"Avatar aleatorio",
    L"Imagen de jugador…",
    L"Se ha guardado el avatar de {0}: los juegos lo muestran a partir de ahora.",
    L"¿Salir del avatar?",
    L"Los cambios no se han guardado.",
    L"Descartar",
    L"Stick derecho o arrastrar: girarlo · arriba y abajo en el stick, o la rueda: acercarse a la cara",
    L"Editar avatar…",
    L"Cambiar imagen de jugador…",
    L"Imagen de jugador de {0}",
    L"La imagen del perfil en el panel y en la Guía de los juegos: su avatar, o una imagen de uno de tus juegos.",
    L"Avatar",
    L"Inicial",
    L"Se ha cambiado la imagen de jugador de {0}.",
    L"No se puede guardar el avatar o la imagen de jugador en {0}.",
    L"Girar",
    L"Cambiar",
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

std::wstring PlayTime(uint64_t seconds) {
  const uint64_t minutes = seconds / 60;
  if (minutes == 0) return Tr(kTextUnderAMinute);
  if (minutes < 60) return Tr(kTextMinutes, {std::to_wstring(minutes)});
  return Tr(kTextHoursMinutes, {std::to_wstring(minutes / 60), std::to_wstring(minutes % 60)});
}

std::wstring ShortDate(const std::string& iso) {
  SYSTEMTIME utc{};
  if (sscanf_s(iso.c_str(), "%4hu-%2hu-%2huT%2hu:%2hu:%2hu", &utc.wYear, &utc.wMonth, &utc.wDay, &utc.wHour,
                  &utc.wMinute, &utc.wSecond) != 6)
    return {};
  SYSTEMTIME local{};
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
  wchar_t text[64];
  const wchar_t* locale = g_language == Language::kSpanish ? L"es-ES" : L"en-GB";
  if (!GetDateFormatEx(locale, 0, &local, g_language == Language::kSpanish ? L"d 'de' MMMM 'de' yyyy" : L"d MMMM yyyy",
                       text, 64, nullptr))
    return {};
  return text;
}

std::wstring Hex32(uint32_t value) {
  wchar_t text[16];
  swprintf(text, 16, L"%08X", value);
  return text;
}

}  // namespace anyx360::launcher
