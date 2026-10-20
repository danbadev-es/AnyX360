# Inter (thirdparty/fonts/inter, SIL Open Font License 1.1), embedded for the in-game panels and the
# Kinect debug window: the TTFs become arrays in a generated source of this game-independent library
# (the GPU sources are compiled once per game).
set(ANYX360_INTER_DIR ${ANYX360_THIRDPARTY}/fonts/inter)
set(inter_cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/inter_fonts.cpp)
set(inter_arrays "")
foreach(weight Regular SemiBold)
  set(ttf ${ANYX360_INTER_DIR}/Inter-${weight}.ttf)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ttf})
  file(READ ${ttf} hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(APPEND inter_arrays "const uint8_t kInter${weight}[] = {${bytes}};\n")
endforeach()
file(CONFIGURE OUTPUT ${inter_cpp} CONTENT "// Generated from thirdparty/fonts/inter (runtime/CMakeLists.txt).
#include \"x360/fonts.h\"

namespace x360::fonts {
namespace {
${inter_arrays}}  // namespace
std::span<const uint8_t> InterRegular() { return kInterRegular; }
std::span<const uint8_t> InterSemiBold() { return kInterSemiBold; }
}  // namespace x360::fonts
")

# Game-independent runtime pieces.
add_library(x360_runtime STATIC
  src/memory.cpp
  ${inter_cpp}
)
target_include_directories(x360_runtime PUBLIC include)

# The Kinect debug window (kinect_window/): its own thread and Direct2D window. Game-independent, so
# not under src/nui (compiled into every game's objects); the nui:: functions it calls are each
# game's, resolved when the game links.
add_library(x360_kinect_window STATIC kinect_window/kinect_window.cpp)
target_include_directories(x360_kinect_window PUBLIC kinect_window PRIVATE include)
target_compile_definitions(x360_kinect_window PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_features(x360_kinect_window PRIVATE cxx_std_20)
target_link_libraries(x360_kinect_window PUBLIC ui_d2d x360_runtime dwmapi)

# --- Per-game kernel / HLE --------------------------------------------------------------------
# PPCContext's layout depends on the optimisation flags in the game's generated ppc_config.h,
# so everything that touches guest registers is compiled once per game against that header.

set(XENON_UTILS_DIR ${ANYX360_THIRDPARTY}/XenonRecomp/XenonUtils)
set(XENON_SIMDE_DIR ${ANYX360_THIRDPARTY}/XenonRecomp/thirdparty/simde)

# Export table without the trailing commas, so it can expand into function definitions.
set(HLE_EXPORT_TABLE ${CMAKE_CURRENT_BINARY_DIR}/generated/hle_export_table.inc)
file(READ ${XENON_UTILS_DIR}/xbox/xboxkrnl_table.inc _krnl)
file(READ ${XENON_UTILS_DIR}/xbox/xam_table.inc _xam)
string(REGEX REPLACE "\\),[ \t]*\n" ")\n" _table "${_krnl}\n${_xam}")
file(CONFIGURE OUTPUT ${HLE_EXPORT_TABLE} CONTENT "${_table}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${XENON_UTILS_DIR}/xbox/xboxkrnl_table.inc ${XENON_UTILS_DIR}/xbox/xam_table.inc)

file(GLOB ANYX360_HLE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hle/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/input/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/nui/*.cpp)
set(ANYX360_HLE_SOURCES ${ANYX360_HLE_SOURCES} CACHE INTERNAL "")
# tools/x360tool/src comes first: XenonUtils also has an xex.h.
set(ANYX360_HLE_INCLUDES
  ${PROJECT_SOURCE_DIR}/tools/x360tool/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${ANYX360_GENERATED_DIR}
  ${XENON_UTILS_DIR}
  ${XENON_SIMDE_DIR}
  CACHE INTERNAL "")

# anyx360_add_hle(<target> <dir containing the game's ppc_config.h>)
# Creates an OBJECT library (so strong exports always win over the weak stubs).
function(anyx360_add_hle target ppc_config_dir)
  add_library(${target} OBJECT ${ANYX360_HLE_SOURCES})
  target_include_directories(${target} PUBLIC ${ppc_config_dir} ${ANYX360_HLE_INCLUDES})
  target_link_libraries(${target} PUBLIC x360_runtime x360_formats x360_kinect_window xe_gpu_d3d12 xe_apu xinput bcrypt dbghelp avrt mfplat mf mfuuid)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PUBLIC -msse4.2)
    target_compile_options(${target} PRIVATE -Wno-unused-function -Wno-multichar -Wno-unused-parameter)
  endif()
endfunction()
