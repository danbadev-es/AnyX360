# Call of Duty: Modern Warfare 3. The recompiled sources are generated locally from the user's own copy
# of the game (see docs/MODERNWARFARE3.md) and are never committed.
anyx360_add_game(mw3 SOURCES src/main.cpp)
# Multiplayer: default_mp.xex, a second program the campaign's menu starts.
anyx360_add_game_executable(mw3 mw3_mp XEX default_mp.xex CONFIG config_mp.toml SOURCES src/main_mp.cpp)
