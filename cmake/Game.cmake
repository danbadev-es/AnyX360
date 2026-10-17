# anyx360_add_game(<id> SOURCES <host sources>...)
# Builds one recompiled game from games/<id>: its game.json (name, executable, user folder, title ID,
# launcher and packaging facts), the XenonRecomp output and the host sources.
#
# The recompiled sources are generated locally from the player's own copy of the game and are never
# committed. Their folder is out_directory_path in games/<id>/config.toml (or -D<ID>_PPC_DIR=<folder>):
# XenonRecomp never deletes old files, so a regeneration goes into a new empty folder (change
# out_directory_path, e.g. recomp2) and the build follows. Games without generated sources are skipped.
#
# Generates game_info.h (namespace game_info: kTitle, kUserDir, kDevGameDir, kTitleId) for the host
# sources, and the game's launcher <id>_launcher.
function(anyx360_add_game id)
  cmake_parse_arguments(GAME "" "" "SOURCES" ${ARGN})
  set(json_path ${CMAKE_CURRENT_SOURCE_DIR}/game.json)
  file(READ ${json_path} json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${json_path})
  string(JSON title GET "${json}" title)
  string(JSON exe GET "${json}" exe)
  string(JSON user_dir GET "${json}" user_dir)
  string(JSON title_id GET "${json}" title_id)
  string(JSON dev_game_dir GET "${json}" dev_game_dir)

  string(TOUPPER ${id} ID)
  if(DEFINED ${ID}_PPC_DIR)
    set(ppc_dir ${${ID}_PPC_DIR})
  else()
    set(config_path ${CMAKE_CURRENT_SOURCE_DIR}/config.toml)
    file(STRINGS ${config_path} out_line REGEX "^out_directory_path *=")
    string(REGEX REPLACE "^out_directory_path *= *\"([^\"]*)\".*" "\\1" out_dir "${out_line}")
    set(ppc_dir ${CMAKE_CURRENT_SOURCE_DIR}/${out_dir})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${config_path})
  endif()
  if(NOT EXISTS ${ppc_dir}/ppc_config.h)
    message(STATUS "${id}: ${ppc_dir} not generated yet, skipping (run XenonRecomp first)")
    return()
  endif()

  # Recompiled code: flags follow UnleashedRecomp (Clang only, the code relies on its builtins).
  file(GLOB ppc_sources CONFIGURE_DEPENDS ${ppc_dir}/*.cpp)
  add_library(${id}_ppc OBJECT ${ppc_sources})
  target_include_directories(${id}_ppc PUBLIC
    ${ppc_dir}
    ${ANYX360_THIRDPARTY}/XenonRecomp/XenonUtils
    ${ANYX360_THIRDPARTY}/XenonRecomp/thirdparty/simde)
  target_compile_options(${id}_ppc PRIVATE
    -msse4.1
    -fno-strict-aliasing
    -Wno-unused-label
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-switch
    -Wno-null-arithmetic
    -Wno-constant-conversion
    -Wno-tautological-constant-out-of-range-compare)
  target_compile_features(${id}_ppc PRIVATE cxx_std_20)
  # Line tables only: enough to name the recompiled function in crash reports.
  target_compile_options(${id}_ppc PRIVATE -gline-tables-only)

  anyx360_add_hle(${id}_hle ${ppc_dir})
  target_compile_options(${id}_hle PRIVATE -gline-tables-only)

  set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
  file(CONFIGURE OUTPUT ${generated}/game_info.h CONTENT "// Generated from games/${id}/game.json.
#pragma once
#include <cstdint>
namespace game_info {
inline constexpr char kTitle[] = \"${title}\";
inline constexpr char kUserDir[] = \"${user_dir}\";
inline constexpr char kDevGameDir[] = \"${dev_game_dir}\";
inline constexpr uint32_t kTitleId = 0x${title_id};
}  // namespace game_info
")

  add_executable(${id} ${GAME_SOURCES})
  target_link_libraries(${id} PRIVATE ${id}_ppc ${id}_hle)
  target_include_directories(${id} PRIVATE ${generated} ${ANYX360_GENERATED_DIR})
  target_compile_options(${id} PRIVATE -msse4.1 -gline-tables-only)
  # A .pdb next to the executable, so crash reports name the recompiled function.
  target_link_options(${id} PRIVATE /DEBUG)
  set_target_properties(${id} PROPERTIES OUTPUT_NAME ${exe})
  anyx360_add_version_resource(${id} "${title} (AnyX360 static recompilation)" ${exe}.exe)

  # The distribution's launcher (tools/package/make_dist.ps1 builds the folder around it).
  anyx360_add_launcher(${id}_launcher ${json_path})
endfunction()
