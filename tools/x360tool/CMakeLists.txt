include(FetchContent)

# libmspack (LGPL-2.1) provides the LZX decoder used by "normal" compressed XEX images.
# Only lzxd.c is built, into its own static library.
FetchContent_Declare(libmspack
  GIT_REPOSITORY https://github.com/kyz/libmspack.git
  GIT_TAG v1.11
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _none  # populate only; libmspack's own build is not used
)
FetchContent_MakeAvailable(libmspack)

set(MSPACK_DIR ${libmspack_SOURCE_DIR}/libmspack/mspack)
add_library(mspack_lzxd STATIC ${MSPACK_DIR}/lzxd.c)
target_include_directories(mspack_lzxd PUBLIC ${MSPACK_DIR})
target_compile_definitions(mspack_lzxd PRIVATE _CRT_SECURE_NO_WARNINGS)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(mspack_lzxd PRIVATE -w)
endif()

add_executable(x360tool
  src/main.cpp
  src/util.cpp
  src/xdvdfs.cpp
  src/xex.cpp
  src/lzx.cpp
)
target_link_libraries(x360tool PRIVATE x360_runtime mspack_lzxd bcrypt)
target_compile_definitions(x360tool PRIVATE _CRT_SECURE_NO_WARNINGS)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME x360tool_e2e
    COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/test_x360tool.py
            $<TARGET_FILE:x360tool> ${CMAKE_CURRENT_BINARY_DIR}/test_work)
endif()
