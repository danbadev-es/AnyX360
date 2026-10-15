# Game-independent runtime pieces.
add_library(x360_runtime STATIC
  src/memory.cpp
)
target_include_directories(x360_runtime PUBLIC include)

# --- Per-game HLE ---------------------------------------------------------------------------
# PPCContext's layout depends on the optimisation flags in the game's generated ppc_config.h,
# so the HLE layer is compiled once per game against that header.

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

set(ANYX360_HLE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hle/hle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hle/stubs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hle/xboxkrnl_ke.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hle/xboxkrnl_rtl.cpp
  CACHE INTERNAL "")
set(ANYX360_HLE_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${XENON_UTILS_DIR}
  ${XENON_SIMDE_DIR}
  CACHE INTERNAL "")

# anyx360_add_hle(<target> <dir containing the game's ppc_config.h>)
# Creates an OBJECT library (so strong exports always win over the weak stubs).
function(anyx360_add_hle target ppc_config_dir)
  add_library(${target} OBJECT ${ANYX360_HLE_SOURCES})
  target_include_directories(${target} PUBLIC ${ppc_config_dir} ${ANYX360_HLE_INCLUDES})
  target_link_libraries(${target} PUBLIC x360_runtime)
  target_compile_features(${target} PUBLIC cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PUBLIC -msse4.2 -Wno-unused-function)
  endif()
endfunction()
