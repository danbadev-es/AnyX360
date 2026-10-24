# anyx360_add_game(<id> SOURCES <host sources>...)
# Builds one recompiled game from games/<id>: its game.json (name, executable, user folder, title ID,
# launcher and packaging facts), the XenonRecomp output and the host sources.
#
# anyx360_add_game_executable(<id> <target> XEX <name.xex> CONFIG <config.toml> SOURCES <host sources>...)
# Builds another executable of the same title (a title can ship several XEX files and start one from
# another, like Modern Warfare 3's default_mp.xex): its own XenonRecomp configuration and output, and
# the executable game.json's "executables" names for that XEX. It isn't offered by the launcher on its
# own; building the game builds it too, so the release packages it (tools/package/make_dist.ps1). Call
# it after anyx360_add_game.
#
# The recompiled sources are generated locally from the player's own copy of the game and are never
# committed. Their folder is out_directory_path in the target's config (config.toml for the game, or
# -D<TARGET>_PPC_DIR=<folder>, e.g. -DMW3_PPC_DIR): XenonRecomp never deletes old files, so a
# regeneration goes into a new empty folder (change out_directory_path, e.g. recomp2) and the build
# follows. Targets without generated sources are skipped.
#
# Generates game_info.h per target (namespace game_info: kTitle, kUserDir, kDevGameDir, kTitleId,
# kXexName, kExecutables, kUpdateVersion, kUpdatePackage) for the host sources, and registers the
# game with the launcher (cmake/Launcher.cmake).
function(anyx360_add_game id)
  cmake_parse_arguments(GAME "" "" "SOURCES" ${ARGN})
  _anyx360_read_game_json(json)
  string(JSON exe GET "${json}" exe)
  _anyx360_add_game_target(${id} ${id} default.xex ${exe} ${CMAKE_CURRENT_SOURCE_DIR}/config.toml ${GAME_SOURCES})
  # One of the games the release's launcher offers.
  if(TARGET ${id})
    anyx360_register_launcher_game(${id} ${CMAKE_CURRENT_SOURCE_DIR}/game.json)
  endif()
endfunction()

function(anyx360_add_game_executable id target)
  cmake_parse_arguments(GAME "" "XEX;CONFIG" "SOURCES" ${ARGN})
  _anyx360_read_game_json(json)
  string(JSON exe ERROR_VARIABLE missing GET "${json}" executables ${GAME_XEX})
  if(missing)
    message(FATAL_ERROR "${id}: game.json has no \"executables\" entry for ${GAME_XEX}")
  endif()
  _anyx360_add_game_target(${target} ${id} ${GAME_XEX} ${exe} ${CMAKE_CURRENT_SOURCE_DIR}/${GAME_CONFIG} ${GAME_SOURCES})
  if(TARGET ${target} AND TARGET ${id})
    add_dependencies(${id} ${target})
  endif()
endfunction()

# Reads games/<id>/game.json into `out_var`.
function(_anyx360_read_game_json out_var)
  set(json_path ${CMAKE_CURRENT_SOURCE_DIR}/game.json)
  file(READ ${json_path} json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${json_path})
  set(${out_var} "${json}" PARENT_SCOPE)
endfunction()

# One executable: <target> recompiled from <xex> with <config_path>, named <exe>.exe.
function(_anyx360_add_game_target target id xex exe config_path)
  set(sources ${ARGN})
  _anyx360_read_game_json(json)
  string(JSON title GET "${json}" title)
  string(JSON user_dir GET "${json}" user_dir)
  string(JSON title_id GET "${json}" title_id)
  string(JSON dev_game_dir GET "${json}" dev_game_dir)
  # Optional: "update": {"version", "package"}, the title update the game is recompiled from.
  string(JSON update_version ERROR_VARIABLE no_update GET "${json}" update version)
  string(JSON update_package ERROR_VARIABLE no_update_package GET "${json}" update package)
  if(no_update OR no_update_package)
    set(update_version "")
    set(update_package "")
  endif()

  string(TOUPPER ${target} TARGET_UPPER)
  if(DEFINED ${TARGET_UPPER}_PPC_DIR)
    set(ppc_dir ${${TARGET_UPPER}_PPC_DIR})
  else()
    file(STRINGS ${config_path} out_line REGEX "^out_directory_path *=")
    string(REGEX REPLACE "^out_directory_path *= *\"([^\"]*)\".*" "\\1" out_dir "${out_line}")
    set(ppc_dir ${CMAKE_CURRENT_SOURCE_DIR}/${out_dir})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${config_path})
  endif()
  if(NOT EXISTS ${ppc_dir}/ppc_config.h)
    message(STATUS "${target}: ${ppc_dir} not generated yet, skipping (run XenonRecomp first)")
    return()
  endif()

  # Recompiled code: flags follow UnleashedRecomp (Clang only, the code relies on its builtins).
  file(GLOB ppc_sources CONFIGURE_DEPENDS ${ppc_dir}/*.cpp)
  add_library(${target}_ppc OBJECT ${ppc_sources})
  target_include_directories(${target}_ppc PUBLIC
    ${ppc_dir}
    ${ANYX360_THIRDPARTY}/XenonRecomp/XenonUtils
    ${ANYX360_THIRDPARTY}/XenonRecomp/thirdparty/simde)
  target_compile_options(${target}_ppc PRIVATE
    -msse4.1
    -fno-strict-aliasing
    -Wno-unused-label
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-switch
    -Wno-null-arithmetic
    -Wno-constant-conversion
    -Wno-tautological-constant-out-of-range-compare)
  target_compile_features(${target}_ppc PRIVATE cxx_std_20)
  # Line tables only: enough to name the recompiled function in crash reports.
  target_compile_options(${target}_ppc PRIVATE -gline-tables-only)

  anyx360_add_hle(${target}_hle ${ppc_dir})
  target_compile_options(${target}_hle PRIVATE -gline-tables-only)

  # Every executable of the title, {XEX, host executable}: one, unless game.json lists them.
  string(JSON count ERROR_VARIABLE single LENGTH "${json}" executables)
  set(executables "")
  if(single)
    set(executables "{\"default.xex\", \"${exe}\"}")
  else()
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
      string(JSON entry_xex MEMBER "${json}" executables ${i})
      string(JSON entry_exe GET "${json}" executables ${entry_xex})
      if(i GREATER 0)
        string(APPEND executables ", ")
      endif()
      string(APPEND executables "{\"${entry_xex}\", \"${entry_exe}\"}")
    endforeach()
  endif()

  set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated/${target})
  file(CONFIGURE OUTPUT ${generated}/game_info.h CONTENT "// Generated from games/${id}/game.json.
#pragma once
#include <cstdint>
namespace game_info {
inline constexpr char kTitle[] = \"${title}\";
inline constexpr char kUserDir[] = \"${user_dir}\";
inline constexpr char kDevGameDir[] = \"${dev_game_dir}\";
inline constexpr uint32_t kTitleId = 0x${title_id};
// The XEX this executable is recompiled from.
inline constexpr char kXexName[] = \"${xex}\";
// Every executable of the title: {XEX, host executable without .exe}, next to each other.
inline constexpr const char* kExecutables[][2] = {${executables}};
// The title update it is recompiled from (GameDescriptor::update_package): empty for none.
inline constexpr char kUpdateVersion[] = \"${update_version}\";
inline constexpr char kUpdatePackage[] = \"${update_package}\";
}  // namespace game_info
")

  # The manifest: per-monitor DPI awareness (sizes are physical pixels), long paths. game.rc: the
  # AnyX360 icon, for the executable and the game's window.
  add_executable(${target} ${sources} ${PROJECT_SOURCE_DIR}/runtime/game.manifest
                 ${PROJECT_SOURCE_DIR}/runtime/game.rc)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/runtime/game.rc PROPERTIES OBJECT_DEPENDS
    ${PROJECT_SOURCE_DIR}/tools/launcher/ui/launcher.ico)
  target_link_libraries(${target} PRIVATE ${target}_ppc ${target}_hle)
  target_include_directories(${target} PRIVATE ${generated} ${ANYX360_GENERATED_DIR})
  target_compile_options(${target} PRIVATE -msse4.1 -gline-tables-only)
  # A .pdb next to the executable, so crash reports name the recompiled function.
  target_link_options(${target} PRIVATE /DEBUG)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${exe})
  anyx360_add_version_resource(${target} "${title} (AnyX360 static recompilation)" ${exe}.exe)
endfunction()
