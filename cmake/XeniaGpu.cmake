# Builds the subset of xenia-canary (BSD) that AnyX360 uses as its GPU back end: base utilities,
# guest memory (with MMIO and write watches), UI/presenter, and the Direct3D 12 GPU emulation.
# Xenia's CPU, kernel, audio and input are not built: AnyX360 provides those.
#
# Target names are prefixed with xe_ because XenonRecomp already defines fmt/xxhash targets.

set(XENIA_ROOT ${ANYX360_THIRDPARTY}/xenia)
set(XENIA_SRC ${XENIA_ROOT}/src/xenia)
set(XE_TARGET_X86_64 TRUE)
set(XE_TARGET_AARCH64 FALSE)
include(${XENIA_ROOT}/cmake/XeniaHelpers.cmake)  # xe_platform_sources()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Common settings for everything built from Xenia's tree.
function(anyx360_xenia_defaults target)
  target_include_directories(${target} PUBLIC
    ${XENIA_ROOT}
    ${XENIA_ROOT}/src
    ${XENIA_ROOT}/third_party
    ${XENIA_ROOT}/third_party/fmt/include
    ${XENIA_ROOT}/third_party/imgui
    ${XENIA_ROOT}/third_party/xxhash
    ${XENIA_ROOT}/third_party/snappy
    ${XENIA_ROOT}/third_party/dxbc
    ${XENIA_ROOT}/third_party/cxxopts/include
    ${XENIA_ROOT}/third_party/tomlplusplus/include
    ${XENIA_ROOT}/third_party/DirectX-Headers/include
    ${XENIA_ROOT}/third_party/utfcpp/source
    ${CMAKE_BINARY_DIR}/xenia_generated)
  target_compile_definitions(${target} PUBLIC
    UNICODE _UNICODE WIN32 _WIN64=1 _AMD64=1 _LIB
    _CRT_NONSTDC_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS
    IMGUI_DISABLE_DEFAULT_FONT IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_USE_STB_SPRINTF
    XE_OPTION_PROFILING=0 XE_OPTION_PROFILING_UI=0 NDEBUG)
  target_compile_features(${target} PUBLIC cxx_std_20)
  target_compile_options(${target} PRIVATE /utf-8 /arch:AVX -w)
endfunction()

# Shader bytecode headers (FXC), generated into <shader_dir>/bytecode/d3d12_5_1 as Xenia expects.
function(anyx360_xenia_dxbc_shaders target shader_dir)
  file(GLOB _sources "${shader_dir}/*.xesl" "${shader_dir}/*.hlsl")
  file(GLOB _includes "${shader_dir}/*.xesli" "${shader_dir}/*.hlsli")
  set(_outputs)
  foreach(src ${_sources})
    get_filename_component(_name ${src} NAME)
    string(REGEX REPLACE "\\.[^.]+$" "" _base "${_name}")
    string(REPLACE "." "_" _id "${_base}")
    string(LENGTH "${_id}" _len)
    if(_len LESS 3)
      continue()
    endif()
    math(EXPR _s "${_len} - 2")
    string(SUBSTRING "${_id}" ${_s} 2 _stage)
    if(NOT _stage MATCHES "^(vs|hs|ds|gs|ps|cs)$")
      continue()
    endif()
    set(_out "${shader_dir}/bytecode/d3d12_5_1/${_id}.h")
    add_custom_command(OUTPUT ${_out}
      COMMAND ${Python3_EXECUTABLE} "${XENIA_ROOT}/tools/build/compile_shader_dxbc.py" "${src}" "${_out}"
      DEPENDS ${src} ${_includes} "${XENIA_ROOT}/tools/build/compile_shader_dxbc.py"
      VERBATIM)
    list(APPEND _outputs ${_out})
  endforeach()
  add_custom_target(${target}_dxbc DEPENDS ${_outputs})
  add_dependencies(${target} ${target}_dxbc)
endfunction()

# version.h normally comes from xenia-build.py.
set(XENIA_GENERATED ${CMAKE_BINARY_DIR}/xenia_generated)
file(WRITE ${XENIA_GENERATED}/version.h.tmp
  "#ifndef GENERATED_VERSION_H_
#define GENERATED_VERSION_H_
"
  "#define XE_BUILD_BRANCH \"anyx360\"
#define XE_BUILD_COMMIT \"anyx360\"
"
  "#define XE_BUILD_COMMIT_SHORT \"anyx360\"
#define XE_BUILD_DATE __DATE__
#endif
")
configure_file(${XENIA_GENERATED}/version.h.tmp ${XENIA_GENERATED}/version.h COPYONLY)

# ---- third party ---------------------------------------------------------------------------
add_library(xe_fmt STATIC ${XENIA_ROOT}/third_party/fmt/src/format.cc ${XENIA_ROOT}/third_party/fmt/src/os.cc)
target_include_directories(xe_fmt PUBLIC ${XENIA_ROOT}/third_party/fmt/include)
target_compile_options(xe_fmt PRIVATE -w)

add_library(xe_xxhash STATIC ${XENIA_ROOT}/third_party/xxhash/xxhash.c)
target_include_directories(xe_xxhash PUBLIC ${XENIA_ROOT}/third_party/xxhash)
target_compile_options(xe_xxhash PRIVATE -w)

if(NOT EXISTS "${XENIA_ROOT}/third_party/snappy/snappy-stubs-public.h")
  # Windows values: no sys/uio.h.
  set(HAVE_SYS_UIO_H_01 0)
  set(PROJECT_VERSION_MAJOR 1)
  set(PROJECT_VERSION_MINOR 2)
  set(PROJECT_VERSION_PATCH 1)
  configure_file(${XENIA_ROOT}/third_party/snappy/snappy-stubs-public.h.in
                 ${XENIA_ROOT}/third_party/snappy/snappy-stubs-public.h)
endif()
add_library(xe_snappy STATIC
  ${XENIA_ROOT}/third_party/snappy/snappy-sinksource.cc
  ${XENIA_ROOT}/third_party/snappy/snappy-stubs-internal.cc
  ${XENIA_ROOT}/third_party/snappy/snappy.cc)
target_include_directories(xe_snappy PUBLIC ${XENIA_ROOT}/third_party/snappy)
target_compile_options(xe_snappy PRIVATE -w)

add_library(xe_imgui STATIC
  ${XENIA_ROOT}/third_party/imgui/imgui.cpp
  ${XENIA_ROOT}/third_party/imgui/imgui_draw.cpp
  ${XENIA_ROOT}/third_party/imgui/imgui_tables.cpp
  ${XENIA_ROOT}/third_party/imgui/imgui_widgets.cpp)
target_include_directories(xe_imgui PUBLIC ${XENIA_ROOT}/third_party/imgui PRIVATE ${XENIA_ROOT}/third_party/stb)
target_compile_definitions(xe_imgui PRIVATE IMGUI_DISABLE_DEFAULT_FONT IMGUI_USE_STB_SPRINTF)
target_compile_options(xe_imgui PRIVATE -w)

add_library(xe_dxbc STATIC ${XENIA_ROOT}/third_party/dxbc/DXBCChecksum.cpp)
target_include_directories(xe_dxbc PUBLIC ${XENIA_ROOT}/third_party/dxbc)
target_compile_options(xe_dxbc PRIVATE -w)

# ---- xenia-base ----------------------------------------------------------------------------
add_library(xe_base STATIC)
xe_platform_sources(xe_base ${XENIA_SRC}/base)
get_target_property(_srcs xe_base SOURCES)
list(FILTER _srcs EXCLUDE REGEX "(console_app_main_|main_init_|main_win\\.cc|main_android)")
set_target_properties(xe_base PROPERTIES SOURCES "${_srcs}")
anyx360_xenia_defaults(xe_base)
target_link_libraries(xe_base PUBLIC xe_fmt ntdll ws2_32 shlwapi dxguid bcrypt)

# ---- guest memory (memory.cc + MMIO handler) ------------------------------------------------
add_library(xe_memory STATIC ${XENIA_SRC}/memory.cc ${XENIA_SRC}/cpu/mmio_handler.cc)
anyx360_xenia_defaults(xe_memory)
target_link_libraries(xe_memory PUBLIC xe_base)

# ---- xenia-ui (window, presenter) ------------------------------------------------------------
add_library(xe_ui STATIC)
xe_platform_sources(xe_ui ${XENIA_SRC}/ui)
get_target_property(_srcs xe_ui SOURCES)
list(FILTER _srcs EXCLUDE REGEX "(_demo\\.|windowed_app_main_|microprofile_drawer)")
set_target_properties(xe_ui PROPERTIES SOURCES "${_srcs}")
anyx360_xenia_defaults(xe_ui)
target_compile_definitions(xe_ui PRIVATE XE_UI_NO_HID=1)  # ImGui without Xenia's HID input system
target_link_libraries(xe_ui PUBLIC xe_base xe_imgui dwmapi dxgi winmm)

add_library(xe_ui_d3d12 STATIC)
xe_platform_sources(xe_ui_d3d12 ${XENIA_SRC}/ui/d3d12)
anyx360_xenia_defaults(xe_ui_d3d12)
anyx360_xenia_dxbc_shaders(xe_ui_d3d12 ${XENIA_SRC}/ui/shaders)
target_link_libraries(xe_ui_d3d12 PUBLIC xe_ui)

# ---- xenia-gpu + Direct3D 12 back end --------------------------------------------------------
add_library(xe_gpu STATIC)
xe_platform_sources(xe_gpu ${XENIA_SRC}/gpu)
get_target_property(_srcs xe_gpu SOURCES)
list(FILTER _srcs EXCLUDE REGEX "(spirv_|trace_dump|trace_viewer|shader_compiler_main)")
set_target_properties(xe_gpu PROPERTIES SOURCES "${_srcs}")
anyx360_xenia_defaults(xe_gpu)
target_link_libraries(xe_gpu PUBLIC xe_memory xe_ui xe_dxbc xe_snappy xe_xxhash)

add_library(xe_gpu_d3d12 STATIC)
xe_platform_sources(xe_gpu_d3d12 ${XENIA_SRC}/gpu/d3d12)
anyx360_xenia_defaults(xe_gpu_d3d12)
anyx360_xenia_dxbc_shaders(xe_gpu_d3d12 ${XENIA_SRC}/gpu/shaders)
target_link_libraries(xe_gpu_d3d12 PUBLIC xe_gpu xe_ui_d3d12)

# ---- Audio: XMA decoding (FFmpeg) and XAudio2 output ----------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/XeniaFFmpeg.cmake)

add_library(xe_apu STATIC
  ${XENIA_SRC}/apu/apu_flags.cc
  ${XENIA_SRC}/apu/audio_driver.cc
  ${XENIA_SRC}/apu/xma_context.cc
  ${XENIA_SRC}/apu/xma_context_fake.cc
  ${XENIA_SRC}/apu/xma_context_master.cc
  ${XENIA_SRC}/apu/xma_context_new.cc
  ${XENIA_SRC}/apu/xma_context_old.cc
  ${XENIA_SRC}/apu/xma_decoder.cc
  ${XENIA_SRC}/apu/xma_register_file.cc
  ${XENIA_SRC}/apu/xaudio2/xaudio2_audio_driver.cc)
anyx360_xenia_defaults(xe_apu)
target_include_directories(xe_apu PRIVATE
  ${XENIA_ROOT}/third_party/FFmpeg
  ${XENIA_ROOT}/third_party/ffmpeg-xenia)
target_link_libraries(xe_apu PUBLIC xe_memory libavcodec libavutil libavformat)
