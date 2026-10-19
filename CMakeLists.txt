cmake_minimum_required(VERSION 3.25)
# The version is in VERSION (Semantic Versioning); CHANGELOG.md lists the changes of each one.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION ANYX360_VERSION LIMIT_COUNT 1)
project(AnyX360 VERSION ${ANYX360_VERSION} LANGUAGES C CXX RC)
include(cmake/Version.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT everywhere: XenonRecomp forces it for its libraries (fmt, xxHash...), and mixing
# runtimes across targets that share those libraries fails to link.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "AnyX360 is developed with clang-cl. Recompiled PPC code relies on Clang builtins; MSVC is untested.")
endif()

enable_testing()

option(ANYX360_BUILD_TOOLS "Build offline tools (x360tool)" ON)
option(ANYX360_BUILD_RUNTIME "Build the shared runtime library" ON)
option(ANYX360_BUILD_RECOMPILERS "Build XenonRecomp/XenonAnalyse and XenosRecomp" ON)

set(ANYX360_THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
if(NOT EXISTS ${ANYX360_THIRDPARTY}/XenonRecomp/thirdparty/libmspack/libmspack)
  message(FATAL_ERROR "Submodules missing. Run: git submodule update --init --recursive")
endif()

if(ANYX360_BUILD_RECOMPILERS)
  add_subdirectory(thirdparty/XenonRecomp)
  add_subdirectory(thirdparty/XenosRecomp)
endif()

option(ANYX360_GPU_D3D12 "Render with Xenia's Direct3D 12 GPU back end" ON)
if(ANYX360_GPU_D3D12)
  include(cmake/XeniaGpu.cmake)
endif()

if(ANYX360_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()

if(ANYX360_BUILD_TOOLS)
  add_subdirectory(tools/x360tool)
endif()

if(ANYX360_BUILD_RUNTIME)
  include(cmake/Launcher.cmake)
  include(cmake/Game.cmake)
  add_subdirectory(tests)
  # One folder per game under games/; a game whose recompiled sources don't exist yet is skipped.
  set(ANYX360_GAMES "" CACHE STRING "Games to build (folders under games/; empty: every one)")
  set(games ${ANYX360_GAMES})
  if(NOT games)
    file(GLOB game_lists RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/games CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/games/*/CMakeLists.txt)
    foreach(list_file IN LISTS game_lists)
      get_filename_component(game ${list_file} DIRECTORY)
      list(APPEND games ${game})
    endforeach()
  endif()
  foreach(game IN LISTS games)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/games/${game}/CMakeLists.txt)
      add_subdirectory(games/${game})
    endif()
  endforeach()
  # The release's launcher, with every game built above.
  anyx360_add_launcher()
endif()
