# Halo: Reach. The recompiled sources are generated locally from the user's own copy of the game (see
# docs/HALOREACH.md) and are never committed.
anyx360_add_game(reach SOURCES src/main.cpp src/fibers.cpp src/network_thread.cpp)
# The game's own DLLs, loaded with XexLoadImage at start: the Waves audio plug-in host, the Waves
# library and its two plug-ins (which import from the library), as in Halo 3.
anyx360_add_title_module(reach waveshell CONFIG config_waveshell.toml)
anyx360_add_title_module(reach waveslib CONFIG config_waveslib.toml)
anyx360_add_title_module(reach l360 CONFIG config_l360.toml)
anyx360_add_title_module(reach q10 CONFIG config_q10.toml)
