# anyx360_add_launcher(<target> <output name> <game executable> <title> <user folder>)
# The Windows launcher of a distribution folder (tools/launcher): extracts the player's disc archive
# and starts <game executable>. <user folder> is the game's folder under %LOCALAPPDATA%\AnyX360.
function(anyx360_add_launcher target output_name game_exe title user_dir)
  add_executable(${target} WIN32
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.cpp
    ${PROJECT_SOURCE_DIR}/tools/launcher/launcher.manifest)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
    ANYX360_GAME_EXE="${game_exe}" ANYX360_GAME_TITLE="${title}" ANYX360_USER_DIR="${user_dir}")
  target_compile_options(${target} PRIVATE /utf-8)
  # The terms of use in English and Spanish, shown and accepted in the launcher (make_dist.ps1 also
  # copies the English text to TERMS.txt).
  set(_terms_en ${PROJECT_SOURCE_DIR}/tools/launcher/terms_en.txt)
  set(_terms_es ${PROJECT_SOURCE_DIR}/tools/launcher/terms_es.txt)
  file(READ ${_terms_en} ANYX360_TERMS_EN)
  file(READ ${_terms_es} ANYX360_TERMS_ES)
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${target}_terms.h
    CONTENT "// Generated from tools/launcher/terms_en.txt and terms_es.txt.
constexpr char kTermsEnUtf8[] = R\"terms(@ANYX360_TERMS_EN@)terms\";
constexpr char kTermsEsUtf8[] = R\"terms(@ANYX360_TERMS_ES@)terms\";
"
    @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_terms_en} ${_terms_es})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${ANYX360_GENERATED_DIR})
  target_compile_definitions(${target} PRIVATE ANYX360_TERMS_HEADER="${target}_terms.h")
  target_link_libraries(${target} PRIVATE comctl32 ole32 shell32 uuid)
  anyx360_add_version_resource(${target} "${title} launcher (AnyX360)" "${output_name}.exe")
endfunction()
