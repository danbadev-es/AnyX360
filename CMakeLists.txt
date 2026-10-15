cmake_minimum_required(VERSION 3.25)
project(AnyX360 LANGUAGES C CXX)

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

if(ANYX360_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()

if(ANYX360_BUILD_TOOLS)
  add_subdirectory(tools/x360tool)
endif()

if(ANYX360_BUILD_RUNTIME)
  add_subdirectory(tests)
endif()
