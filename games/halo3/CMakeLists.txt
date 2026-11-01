# Halo 3. The recompiled sources are generated locally from the user's own copy of the game (see
# docs/HALO3.md) and are never committed.
anyx360_add_game(halo3 SOURCES src/main.cpp src/fibers.cpp)
# The game's own DLLs, loaded with XexLoadImage at start: the Waves audio plug-in host, the Waves
# library and its two plug-ins (which import from the library).
anyx360_add_title_module(halo3 waveshell CONFIG config_waveshell.toml)
anyx360_add_title_module(halo3 waveslib CONFIG config_waveslib.toml)
anyx360_add_title_module(halo3 l360 CONFIG config_l360.toml)
anyx360_add_title_module(halo3 q10 CONFIG config_q10.toml)
