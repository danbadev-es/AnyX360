# Versioning: the version (Semantic Versioning, https://semver.org) is in the VERSION file at the
# root, and CHANGELOG.md lists the changes of each version. The build adds the git commit as build
# metadata ("0.1.0+0b3cb8d", ".dirty" with uncommitted changes) and generates:
#   anyx360_version.h          ANYX360_VERSION, ANYX360_VERSION_FULL, ANYX360_GIT_COMMIT
#   anyx360_add_version_resource(<target> <description> <file name>)
#                              the Windows version resource (file properties) of an executable
set(ANYX360_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                  OUTPUT_VARIABLE _commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _result)
  if(_result EQUAL 0)
    set(ANYX360_GIT_COMMIT "${_commit}")
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
                    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE _changes ERROR_QUIET)
    if(NOT _changes STREQUAL "")
      string(APPEND ANYX360_GIT_COMMIT ".dirty")
    endif()
  endif()
endif()
set(ANYX360_VERSION_FULL "${PROJECT_VERSION}+${ANYX360_GIT_COMMIT}")
message(STATUS "AnyX360 ${ANYX360_VERSION_FULL}")

set(ANYX360_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
configure_file(${PROJECT_SOURCE_DIR}/cmake/anyx360_version.h.in ${ANYX360_GENERATED_DIR}/anyx360_version.h @ONLY)
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/VERSION)

function(anyx360_add_version_resource target description file_name)
  set(ANYX360_RC_DESCRIPTION "${description}")
  set(ANYX360_RC_FILE_NAME "${file_name}")
  set(ANYX360_RC_INTERNAL_NAME "${target}")
  configure_file(${PROJECT_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc @ONLY)
  target_sources(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc)
endfunction()
