# anyx360_add_launcher(<target> <game.json>)
# The Windows launcher of a distribution folder (tools/launcher): prepares the player's copy of the
# game and starts the game executable. Names come from the game's game.json (see cmake/Game.cmake).
function(anyx360_add_launcher target json_path)
  file(READ ${json_path} json)
  string(JSON output_name GET "${json}" launcher_name)
  string(JSON exe GET "${json}" exe)
  string(JSON title GET "${json}" title)
  string(JSON user_dir GET "${json}" user_dir)
  string(JSON size_gb GET "${json}" size_gb)
  string(JSON input GET "${json}" input)
  string(JSON owners GET "${json}" owners)
  add_executable(${target} WIN32
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.cpp
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.manifest)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
  if(input STREQUAL "webcam")
    set(webcam 1)
  else()
    set(webcam 0)
  endif()
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
    ANYX360_GAME_EXE="${exe}.exe" ANYX360_GAME_TITLE="${title}" ANYX360_USER_DIR="${user_dir}"
    ANYX360_GAME_SIZE_GB=${size_gb} ANYX360_GAME_WEBCAM=${webcam})
  target_compile_options(${target} PRIVATE /utf-8)

  # The terms of use in English and Spanish, shown and accepted in the launcher, with the game's name,
  # rights holders, trademarks and extra components filled in. The filled texts are also written next
  # to the header (<target>_terms_en.txt, _es.txt): make_dist.ps1 ships the English one as TERMS.txt.
  string(JSON trademark_count LENGTH "${json}" trademarks)
  set(trademarks_en "")
  set(trademarks_es "")
  if(trademark_count GREATER 0)
    math(EXPR last "${trademark_count} - 1")
    foreach(i RANGE ${last})
      string(JSON mark GET "${json}" trademarks ${i})
      if(i GREATER 0)
        string(APPEND trademarks_en ", ")
        string(APPEND trademarks_es ", ")
      endif()
      string(APPEND trademarks_en "\"${mark}\"")
      string(APPEND trademarks_es "«${mark}»")
    endforeach()
  endif()
  string(JSON component_count LENGTH "${json}" components)
  set(components "")
  if(component_count GREATER 0)
    math(EXPR last "${component_count} - 1")
    foreach(i RANGE ${last})
      string(JSON component GET "${json}" components ${i})
      string(APPEND components ", ${component}")
    endforeach()
  endif()
  foreach(language en es)
    set(source ${PROJECT_SOURCE_DIR}/tools/launcher/terms_${language}.txt)
    file(READ ${source} text)
    string(REPLACE "{GAME}" "${title}" text "${text}")
    string(REPLACE "{OWNERS}" "${owners}" text "${text}")
    string(REPLACE "{TRADEMARKS}" "${trademarks_${language}}" text "${text}")
    string(REPLACE "{COMPONENTS}" "${components}" text "${text}")
    set(ANYX360_TERMS_${language} "${text}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}_terms_${language}.txt CONTENT "${text}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
  endforeach()
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}_terms.h
    CONTENT "// Generated from tools/launcher/terms_en.txt and terms_es.txt.
constexpr char kTermsEnUtf8[] = R\"terms(@ANYX360_TERMS_en@)terms\";
constexpr char kTermsEsUtf8[] = R\"terms(@ANYX360_TERMS_es@)terms\";
"
    @ONLY)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${ANYX360_GENERATED_DIR})
  target_compile_definitions(${target} PRIVATE ANYX360_TERMS_HEADER="${target}_terms.h")
  target_link_libraries(${target} PRIVATE comctl32 ole32 shell32 uuid)
  anyx360_add_version_resource(${target} "${title} launcher (AnyX360)" "${output_name}.exe")
endfunction()
