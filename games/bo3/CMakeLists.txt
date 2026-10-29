# Call of Duty: Black Ops III. The recompiled sources are generated locally from the user's own copy of
# the game (see docs/BLACKOPS3.md) and are never committed.
anyx360_add_game(bo3 SOURCES src/main.cpp)
# Zombies: default_zm.xex, a second program the multiplayer executable starts.
anyx360_add_game_executable(bo3 bo3_zm XEX default_zm.xex CONFIG config_zm.toml SOURCES src/main_zm.cpp)
