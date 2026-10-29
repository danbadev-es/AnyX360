# Forza Motorsport 4. The recompiled sources are generated locally from the user's own copy of the
# game (see docs/FORZAMOTORSPORT4.md) and are never committed.
anyx360_add_game(fm4 SOURCES src/main.cpp src/probe.cpp)
# The game's own DLLs, loaded with XexLoadImage at start: the WMV video decoder and the Kinect
# speech recognizer.
anyx360_add_title_module(fm4 xmedia CONFIG config_xmedia.toml)
anyx360_add_title_module(fm4 speech CONFIG config_speech.toml)
