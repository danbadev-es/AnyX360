# Gears of War 3. The recompiled sources in recomp/ are generated locally from the user's own copy of
# the game (see docs/GEARS3.md) and are never committed.

set(GOW3_PPC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/recomp)
if(NOT EXISTS ${GOW3_PPC_DIR}/ppc_config.h)
  message(STATUS "gow3: ${GOW3_PPC_DIR} not generated yet, skipping (run XenonRecomp first)")
  return()
endif()

file(GLOB GOW3_PPC_SOURCES CONFIGURE_DEPENDS ${GOW3_PPC_DIR}/*.cpp)

# Recompiled code: flags follow UnleashedRecomp (Clang only, the code relies on its builtins).
add_library(gow3_ppc OBJECT ${GOW3_PPC_SOURCES})
target_include_directories(gow3_ppc PUBLIC
  ${GOW3_PPC_DIR}
  ${ANYX360_THIRDPARTY}/XenonRecomp/XenonUtils
  ${ANYX360_THIRDPARTY}/XenonRecomp/thirdparty/simde)
target_compile_options(gow3_ppc PRIVATE
  -msse4.1
  -fno-strict-aliasing
  -Wno-unused-label
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-switch
  -Wno-null-arithmetic
  -Wno-constant-conversion
  -Wno-tautological-constant-out-of-range-compare)
target_compile_features(gow3_ppc PRIVATE cxx_std_20)
# Line tables only: enough to name the recompiled function in crash reports.
target_compile_options(gow3_ppc PRIVATE -gline-tables-only)

anyx360_add_hle(gow3_hle ${GOW3_PPC_DIR})

add_executable(gow3 src/main.cpp)
target_link_libraries(gow3 PRIVATE gow3_ppc gow3_hle)
target_compile_options(gow3 PRIVATE -msse4.1)
set_target_properties(gow3 PROPERTIES OUTPUT_NAME GearsOfWar3)
target_compile_options(gow3 PRIVATE -gline-tables-only)
target_link_options(gow3 PRIVATE /DEBUG)
target_compile_options(gow3_hle PRIVATE -gline-tables-only)
