# anyx360_register_launcher_game(<id> <game.json>)
# anyx360_add_launcher()
# The Windows launcher of a release (tools/launcher, AnyX360.exe): a gallery of the games the player
# has added, from their own copies. Each game registers itself (cmake/Game.cmake); once every game
# is known, anyx360_add_launcher() builds the one launcher with a catalog of them all.

function(anyx360_register_launcher_game id json_path)
  set_property(GLOBAL APPEND PROPERTY ANYX360_LAUNCHER_GAMES "${id}")
  set_property(GLOBAL PROPERTY ANYX360_LAUNCHER_JSON_${id} "${json_path}")
endfunction()

# "a", "a and b", "a, b and c" (`and` is the language's word).
function(_anyx360_join_list out_var and_word)
  set(items ${ARGN})
  list(LENGTH items count)
  set(text "")
  set(i 0)
  foreach(item IN LISTS items)
    math(EXPR i "${i} + 1")
    if(i GREATER 1)
      math(EXPR last "${count}")
      if(i EQUAL last)
        string(APPEND text " ${and_word} ")
      else()
        string(APPEND text ", ")
      endif()
    endif()
    string(APPEND text "${item}")
  endforeach()
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(anyx360_add_launcher)
  get_property(ids GLOBAL PROPERTY ANYX360_LAUNCHER_GAMES)
  if(NOT ids)
    message(STATUS "launcher: no games to offer yet, skipping")
    return()
  endif()

  # The catalog: one entry per game (struct CatalogGame). dev_exe is
  # where the build puts the game, used when the launcher runs from the build folder.
  set(entries "")
  set(titles "")
  set(owners "")
  set(trademarks "")
  set(components "")
  foreach(id IN LISTS ids)
    get_property(json_path GLOBAL PROPERTY ANYX360_LAUNCHER_JSON_${id})
    file(READ ${json_path} json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${json_path})
    foreach(field title exe user_dir title_id size_gb input owners)
      string(JSON ${field} GET "${json}" ${field})
    endforeach()
    # Optional: the cover's slug (covers/<slug>.jpg) and whether the game is playable or in testing.
    string(JSON cover ERROR_VARIABLE no_cover GET "${json}" cover)
    if(no_cover)
      set(cover "")
    endif()
    string(JSON status ERROR_VARIABLE no_status GET "${json}" status)
    if(status STREQUAL "playable")
      set(playable true)
    else()
      set(playable false)
    endif()
    set(required_files "")
    string(JSON count LENGTH "${json}" required_files)
    if(count GREATER 0)
      math(EXPR last "${count} - 1")
      foreach(i RANGE ${last})
        string(JSON file GET "${json}" required_files ${i})
        if(i GREATER 0)
          string(APPEND required_files "|")
        endif()
        string(APPEND required_files "${file}")
      endforeach()
    endif()
    if(input STREQUAL "webcam")
      set(webcam true)
    else()
      set(webcam false)
    endif()
    # Optional: "downloadable_content": false when the game's packs need its title update (which
    # AnyX360 doesn't play) and get in the way without it; the launcher then leaves them out.
    string(JSON content ERROR_VARIABLE no_content GET "${json}" downloadable_content)
    if(no_content OR content)
      set(content true)
    else()
      set(content false)
    endif()
    # Optional: "content_needs_update": the packs (package file names) that need the title update when
    # the game's others don't; the launcher leaves those out.
    set(content_needs_update "")
    string(JSON count ERROR_VARIABLE no_list LENGTH "${json}" content_needs_update)
    if(NOT no_list AND count GREATER 0)
      math(EXPR last "${count} - 1")
      foreach(i RANGE ${last})
        string(JSON package GET "${json}" content_needs_update ${i})
        if(i GREATER 0)
          string(APPEND content_needs_update "|")
        endif()
        string(APPEND content_needs_update "${package}")
      endforeach()
    endif()
    # The executable AnyX360 was recompiled from ("xex": its execution info's version and media ID):
    # a copy of another version (an update, or a re-release under the same title ID) has other code.
    string(JSON xex_version ERROR_VARIABLE no_version GET "${json}" xex version)
    string(JSON xex_media_id ERROR_VARIABLE no_media_id GET "${json}" xex media_id)
    if(no_version OR no_media_id OR NOT xex_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$"
       OR NOT xex_media_id MATCHES "^[0-9A-Fa-f]+$")
      message(FATAL_ERROR "${json_path}: \"xex\": {\"version\": \"a.b.c.d\", \"media_id\": \"XXXXXXXX\"} is required")
    endif()
    # Optional: "update": {"version", "package"}, the title update the game is recompiled from ("xex"
    # stays the disc's version, which the copy holds). The game is ready only with it installed.
    set(update_version "")
    set(update_package "")
    string(JSON update_json ERROR_VARIABLE no_update GET "${json}" update)
    if(NOT no_update)
      string(JSON update_version ERROR_VARIABLE bad_version GET "${json}" update version)
      string(JSON update_package ERROR_VARIABLE bad_package GET "${json}" update package)
      if(bad_version OR bad_package OR NOT update_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$"
         OR update_package STREQUAL "" OR update_package MATCHES "[\\\\/:*?\"<>|]")
        message(FATAL_ERROR "${json_path}: \"update\": {\"version\": \"a.b.c.d\", \"package\": \"<package file name>\"}")
      endif()
    endif()
    string(APPEND entries "    {L\"${id}\", L\"${title}\", L\"${exe}.exe\", L\"${user_dir}\", 0x${title_id}, ${size_gb}, ${webcam},\n"
                          "     L\"${required_files}\", L\"$<TARGET_FILE:${id}>\", L\"${cover}\", ${playable}, ${content},\n"
                          "     L\"${content_needs_update}\", L\"${xex_version}\", 0x${xex_media_id}, L\"${update_version}\",\n"
                          "     L\"${update_package}\"},\n")
    list(APPEND titles "${title}")
    # Rights holders, trademarks and extra components of every game, each named once.
    string(REPLACE ", " ";" game_owners "${owners}")
    list(APPEND all_owners ${game_owners})
    foreach(list_name trademarks components)
      string(JSON count LENGTH "${json}" ${list_name})
      if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
          string(JSON item GET "${json}" ${list_name} ${i})
          list(APPEND all_${list_name} "${item}")
        endforeach()
      endif()
    endforeach()
  endforeach()
  # The games coming soon (tools/launcher/upcoming.json): shown greyed in Compatible with their box
  # art, so their owners and trademarks are named in the terms too.
  set(upcoming_path ${PROJECT_SOURCE_DIR}/tools/launcher/upcoming.json)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${upcoming_path})
  file(READ ${upcoming_path} upcoming_json)
  set(upcoming_entries "")
  string(JSON upcoming_count LENGTH "${upcoming_json}" games)
  if(upcoming_count GREATER 0)
    math(EXPR last "${upcoming_count} - 1")
    foreach(i RANGE ${last})
      string(JSON game GET "${upcoming_json}" games ${i})
      foreach(field title cover title_id owners)
        string(JSON ${field} GET "${game}" ${field})
      endforeach()
      if(title_id STREQUAL "")
        set(title_id 0)
      endif()
      string(APPEND upcoming_entries "    {L\"${title}\", L\"${cover}\", 0x${title_id}},\n")
      string(REPLACE ", " ";" game_owners "${owners}")
      list(APPEND all_owners ${game_owners})
      string(JSON marks_count LENGTH "${game}" trademarks)
      if(marks_count GREATER 0)
        math(EXPR marks_last "${marks_count} - 1")
        foreach(j RANGE ${marks_last})
          string(JSON item GET "${game}" trademarks ${j})
          list(APPEND all_trademarks "${item}")
        endforeach()
      endif()
    endforeach()
  endif()

  foreach(list_name owners trademarks components)
    if(all_${list_name})
      list(REMOVE_DUPLICATES all_${list_name})
    endif()
  endforeach()

  set(dir ${CMAKE_BINARY_DIR}/launcher)
  # Per configuration: dev_exe names the configuration's build of each game.
  file(GENERATE OUTPUT ${dir}/$<CONFIG>/games_catalog.h CONTENT "// Generated from games/*/game.json (cmake/Launcher.cmake).
#pragma once
#include <cstdint>

struct CatalogGame {
  const wchar_t* id;
  const wchar_t* title;
  const wchar_t* exe;       // in the release's bin folder
  const wchar_t* user_dir;  // %LOCALAPPDATA%\\AnyX360\\<user_dir>
  uint32_t title_id;
  int size_gb;              // free space an extracted copy needs
  bool webcam;              // played with a webcam (virtual Kinect)
  const wchar_t* required_files;  // besides default.xex, '|'-separated
  const wchar_t* dev_exe;   // the build's copy, when the launcher runs from the build folder
  const wchar_t* cover;     // covers/<cover>.jpg (empty: none)
  bool playable;            // playable; false: in testing
  bool content = true;      // its downloadable content is installed (false: it needs the title update)
  // Its packs that need the title update when the others don't: package file names, '|'-separated.
  const wchar_t* content_needs_update = L\"\";
  // The version of the executable it was recompiled from (\"a.b.c.d\"), and its media ID.
  const wchar_t* xex_version = L\"\";
  uint32_t xex_media_id = 0;
  // The title update it was recompiled from: the version the update makes of xex_version, and the
  // folder it is installed in (content\\000B0000\\<package>). Empty: none, the disc's version is played.
  const wchar_t* update_version = L\"\";
  const wchar_t* update_package = L\"\";
};

inline constexpr CatalogGame kCatalog[] = {
${entries}};

// Shown greyed as \"Coming soon\" (tools/launcher/upcoming.json).
struct UpcomingGame {
  const wchar_t* title;
  const wchar_t* cover;     // covers/<cover>.jpg (empty: a placeholder)
  uint32_t title_id;        // 0: not known yet
};

inline constexpr UpcomingGame kUpcoming[] = {
${upcoming_entries}};
")

  # The terms of use in English and Spanish, shown and accepted in the launcher, naming every game,
  # rights holder, trademark and extra component. The filled texts are also written next to the
  # header (terms_en.txt, terms_es.txt): make_dist.ps1 ships the English one as TERMS.txt.
  string(REPLACE ";" ", " owners_text "${all_owners}")
  set(components_text "")
  foreach(component IN LISTS all_components)
    string(APPEND components_text ", ${component}")
  endforeach()
  set(quoted_en "")
  set(quoted_es "")
  foreach(mark IN LISTS all_trademarks)
    list(APPEND quoted_en "\"${mark}\"")
    list(APPEND quoted_es "«${mark}»")
  endforeach()
  _anyx360_join_list(games_en "and" ${titles})
  _anyx360_join_list(games_es "y" ${titles})
  string(REPLACE ";" ", " trademarks_en "${quoted_en}")
  string(REPLACE ";" ", " trademarks_es "${quoted_es}")
  foreach(language en es)
    set(source ${PROJECT_SOURCE_DIR}/tools/launcher/terms_${language}.txt)
    file(READ ${source} text)
    string(REPLACE "{GAMES}" "${games_${language}}" text "${text}")
    string(REPLACE "{OWNERS}" "${owners_text}" text "${text}")
    string(REPLACE "{TRADEMARKS}" "${trademarks_${language}}" text "${text}")
    string(REPLACE "{COMPONENTS}" "${components_text}" text "${text}")
    set(ANYX360_TERMS_${language} "${text}")
    file(CONFIGURE OUTPUT ${dir}/terms_${language}.txt CONTENT "${text}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
  endforeach()
  file(CONFIGURE OUTPUT ${dir}/launcher_terms.h
    CONTENT "// Generated from tools/launcher/terms_en.txt and terms_es.txt.
constexpr char kTermsEnUtf8[] = R\"terms(@ANYX360_TERMS_en@)terms\";
constexpr char kTermsEsUtf8[] = R\"terms(@ANYX360_TERMS_es@)terms\";
"
    @ONLY)

  # The launcher's logic, without a window: the library, adding and removing games, the text, the
  # terms, starting a game (tools/launcher/core). Tested by test_launcher_core.
  set(core ${PROJECT_SOURCE_DIR}/tools/launcher/core)
  add_library(launcher_core STATIC
    ${core}/anyx.cpp
    ${core}/catalog.cpp
    ${core}/hash.cpp
    ${core}/install.cpp
    ${core}/jobs.cpp
    ${core}/launch.cpp
    ${core}/library.cpp
    ${core}/paths.cpp
    ${core}/probe.cpp
    ${core}/sevenzip.cpp
    ${core}/strings.cpp
    ${core}/terms.cpp)
  target_compile_definitions(launcher_core PUBLIC UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_compile_options(launcher_core PRIVATE /utf-8)
  target_include_directories(launcher_core PUBLIC ${PROJECT_SOURCE_DIR}/tools/launcher ${dir} ${dir}/$<CONFIG>)
  # The .anyx manifest is JSON (rapidjson, header-only); its written_by names the release.
  target_include_directories(launcher_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/xenia/third_party/rapidjson/include)
  target_include_directories(launcher_core PRIVATE ${ANYX360_GENERATED_DIR})
  target_link_libraries(launcher_core PUBLIC x360_formats ole32 shell32 shlwapi bcrypt)

  add_executable(test_launcher_core ${PROJECT_SOURCE_DIR}/tests/launcher/test_launcher_core.cpp)
  target_compile_options(test_launcher_core PRIVATE /utf-8)
  target_link_libraries(test_launcher_core PRIVATE launcher_core)
  add_test(NAME launcher_core COMMAND test_launcher_core)

  # The window (tools/launcher/ui): Direct2D on launcher_core.
  set(ui ${PROJECT_SOURCE_DIR}/tools/launcher/ui)
  add_executable(anyx360_launcher WIN32
    ${ui}/main.cpp
    ${ui}/app.cpp
    ${ui}/images.cpp
    ${ui}/launcher.rc
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.manifest)
  # The icon and the logos it embeds (the build doesn't see what a .rc names on its own).
  set_source_files_properties(${ui}/launcher.rc PROPERTIES OBJECT_DEPENDS
    "${ui}/launcher.ico;${ui}/logo_mark.png;${ui}/logo_full.png")
  set_target_properties(anyx360_launcher PROPERTIES OUTPUT_NAME AnyX360 RUNTIME_OUTPUT_DIRECTORY ${dir}/$<CONFIG>)
  target_compile_options(anyx360_launcher PRIVATE /utf-8)
  target_include_directories(anyx360_launcher PRIVATE ${ANYX360_GENERATED_DIR})
  target_link_libraries(anyx360_launcher PRIVATE launcher_core ui_d2d ui_wic dwmapi xinput ole32 shell32 uuid)
  anyx360_add_version_resource(anyx360_launcher "AnyX360 launcher" "AnyX360.exe")
endfunction()
