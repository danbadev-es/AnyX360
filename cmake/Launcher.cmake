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

  # The catalog: one entry per game (tools/launcher/launcher.cpp, struct CatalogGame). dev_exe is
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
    string(APPEND entries "    {L\"${id}\", L\"${title}\", L\"${exe}.exe\", L\"${user_dir}\", 0x${title_id}, ${size_gb}, ${webcam},\n"
                          "     L\"${required_files}\", L\"$<TARGET_FILE:${id}>\"},\n")
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
};

inline constexpr CatalogGame kCatalog[] = {
${entries}};
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

  add_executable(anyx360_launcher WIN32
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.cpp
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.manifest)
  set_target_properties(anyx360_launcher PROPERTIES OUTPUT_NAME AnyX360 RUNTIME_OUTPUT_DIRECTORY ${dir}/$<CONFIG>)
  target_compile_definitions(anyx360_launcher PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_compile_options(anyx360_launcher PRIVATE /utf-8)
  target_include_directories(anyx360_launcher PRIVATE ${dir} ${dir}/$<CONFIG> ${ANYX360_GENERATED_DIR})
  target_link_libraries(anyx360_launcher PRIVATE x360_formats comctl32 ole32 shell32 shlwapi uuid gdiplus)
  anyx360_add_version_resource(anyx360_launcher "AnyX360 launcher" "AnyX360.exe")
endfunction()
