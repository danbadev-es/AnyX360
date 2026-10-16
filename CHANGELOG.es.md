# Registro de cambios

Todos los cambios relevantes de AnyX360 se documentan en este archivo (English: [CHANGELOG.md](CHANGELOG.md)).

El formato se basa en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto
sigue el [Versionado Semántico](https://semver.org/lang/es/spec/v2.0.0.html). La versión está en el
archivo [VERSION](VERSION); las compilaciones añaden el commit de git como metadatos de compilación
(`0.1.0+0b3cb8d`).

## [Sin publicar]

## [0.1.0] - 2026-10-16

Primera versión distribuida a probadores. Juego: Gears of War 3 (título 4D5308AB, ejecutable del
disco base), con la copia del propio jugador.

### Añadido

- Recompilación estática del ejecutable completo de Gears of War 3 con versiones propias de
  XenonRecomp y XenonAnalyse (tablas de saltos, límites de funciones, instrucciones que faltaban).
- Reimplementación del kernel y de XAM (memoria, hilos, objetos del dispatcher, archivos,
  contenido y perfiles, notificaciones, sockets locales): el juego arranca y se juegan la campaña y
  la Horda privada.
- Renderizado con el backend Direct3D 12 de Xenia y caché persistente de shaders y pipelines.
- Decodificación de audio XMA y salida por XAudio2, sincronizada con el dispositivo de salida.
- Opciones de pantalla: ventana de 720p a 4K, escala de render 1x-3x, escalado AMD FSR/CAS o
  bilineal, FXAA, pantalla completa, 30 o 60 FPS, VSync.
- Panel de ajustes (F2), estadísticas de rendimiento (F1) y ajustes persistentes (`settings.toml`).
- Controles de teclado y ratón al estilo Gears con cámara por raw input; mandos de Xbox.
- Pantalla dividida local para hasta cuatro jugadores, con perfiles y partidas por jugador.
- Título de la ventana leído de los datos XDBF del propio juego.
- Instrumentación de tiempos de fotograma (`ANYX360_FRAMETIMES`, `tools/scripts/frametimes.py`).
- Launcher para Windows en inglés y español: términos de uso, extracción del `.7z` del jugador,
  registros del juego y aviso de errores.
- Empaquetado de la distribución (`tools/package/make_dist.ps1`) con README/LEEME, términos de uso,
  licencias de terceros y este registro de cambios.
- Versionado: `VERSION`, versión visible en el launcher, en el registro del juego y en el panel F2,
  e información de versión de Windows en los ejecutables.

### Corregido

- Fotogramas irregulares: vblank preciso y sincronizado con el refresco de la pantalla, el
  dispatcher despierta solo a las esperas que una señal satisface, y el overlay ya no obliga a
  presentar desde el hilo de la interfaz.
- Cierre a los cinco minutos de campaña (`NetDll_inet_addr` leía su cadena del argumento equivocado).
- Artefactos en los vídeos Bink, audio distorsionado y límite de 30 FPS.

[Sin publicar]: https://github.com/danielbarretoes/AnyX360/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/danielbarretoes/AnyX360/releases/tag/v0.1.0
