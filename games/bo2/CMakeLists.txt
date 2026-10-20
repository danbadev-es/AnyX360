# Call of Duty: Black Ops II. The recompiled sources are generated locally from the user's own copy of
# the game (see docs/BLACKOPS2.md) and are never committed.
anyx360_add_game(bo2 SOURCES src/main.cpp)
# Multiplayer and Zombies: default_mp.xex, a second program the campaign's menu starts.
anyx360_add_game_executable(bo2 bo2_mp XEX default_mp.xex CONFIG config_mp.toml SOURCES src/main_mp.cpp)
