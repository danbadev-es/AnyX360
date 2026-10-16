# FFmpeg subset (libavutil, libavcodec with the XMA decoder, libavformat) built from xenia-canary's
# FFmpeg fork. Generated from the FFmpeg section of thirdparty/xenia/third_party/CMakeLists.txt with
# absolute paths; regenerate if that section changes. FFmpeg is LGPL 2.1+.

set(XE_HAS_GAS_ASM FALSE)

# Helper: compile GAS-syntax .S files with clang on MSVC, or natively on
# GCC/Clang. Adds the resulting objects to the target.
function(xe_add_gas_sources target)
  if(NOT XE_HAS_GAS_ASM)
    return()
  endif()
  foreach(src ${ARGN})
    if(XE_CLANG_ASM)
      # MSVC: use clang to assemble .S -> .obj
      get_filename_component(src_name "${src}" NAME_WE)
      get_filename_component(src_abs "${src}" ABSOLUTE)
      set(obj "${CMAKE_CURRENT_BINARY_DIR}/${src_name}.obj")
      add_custom_command(
        OUTPUT "${obj}"
        COMMAND "${XE_CLANG_ASM}" --target=aarch64-pc-windows-msvc
                -I "${XENIA_ROOT}/third_party/FFmpeg"
                -I "${XENIA_ROOT}/third_party/ffmpeg-xenia"
                -c "${src_abs}" -o "${obj}"
        DEPENDS "${src_abs}"
        COMMENT "Assembling ${src} with clang"
      )
      target_sources(${target} PRIVATE "${obj}")
    else()
      # GCC/Clang: native ASM support
      target_sources(${target} PRIVATE "${src}")
    endif()
  endforeach()
endfunction()

# Common function to apply shared FFmpeg settings to a target.
function(ffmpeg_common target)
  target_compile_definitions(${target} PRIVATE
    HAVE_AV_CONFIG_H
    _USE_MATH_DEFINES
  )
  target_include_directories(${target} PRIVATE
    ${XENIA_ROOT}/third_party/ffmpeg-xenia
    ${XENIA_ROOT}/third_party/FFmpeg
  )

  if(WIN32)
    target_include_directories(${target} PRIVATE
      ${XENIA_ROOT}/third_party/FFmpeg/compat/atomics/win32
    )
    target_link_libraries(${target} PRIVATE bcrypt)
    # Size optimization on Debug/Checked (dead code elimination is mandatory)
    target_compile_options(${target} PRIVATE
      $<$<CONFIG:Debug>:/Os>
      $<$<CONFIG:Checked>:/Os>
    )
  else()
    target_include_directories(${target} PRIVATE
      ${XENIA_ROOT}/third_party/FFmpeg/compat/atomics/gcc
    )
  endif()

  # Suppress all warnings for FFmpeg code (recent Clang makes this one an error by default)
  if(MSVC)
    target_compile_options(${target} PRIVATE /w -Wno-incompatible-pointer-types)
  else()
    target_compile_options(${target} PRIVATE -w)
  endif()

  # All FFmpeg source is C
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
endfunction()

# --- libavutil ---
add_library(libavutil STATIC
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/adler32.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aes.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aes_ctr.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/audio_fifo.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/avstring.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/avsscanf.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/base64.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/blowfish.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/bprint.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/buffer.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/cast5.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/camellia.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/channel_layout.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/csp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/cpu.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/crc.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/des.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/dict.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/display.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/dovi_meta.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/downmix_info.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/encryption_info.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/error.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/eval.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/fifo.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/file.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/file_open.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/float_dsp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/fixed_dsp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/frame.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/hash.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/hdr_dynamic_metadata.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/hmac.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/hwcontext.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/imgutils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/integer.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/intmath.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/lfg.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/lls.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/log.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/log2_tab.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/mathematics.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/mastering_display_metadata.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/md5.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/mem.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/murmur3.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/opt.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/parseutils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/pixdesc.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/pixelutils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/random_seed.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/rational.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/reverse.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/rc4.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/ripemd.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/samplefmt.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/sha.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/sha512.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/slicethread.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/spherical.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/stereo3d.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/threadmessage.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/time.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/timecode.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tree.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/twofish.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/utils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/xga_font_data.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/xtea.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tea.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tx.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tx_float.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tx_double.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/tx_int32.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/video_enc_params.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/film_grain_params.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/side_data.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/refstruct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/container_fifo.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/float_scalarproduct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/timecode_internal.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/iamf.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavutil/timestamp.c
)
# x86 platform files
if(XE_TARGET_X86_64)
  target_sources(libavutil PRIVATE
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/cpu.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/fixed_dsp_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/float_dsp_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/imgutils_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/lls_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/x86/tx_float_init.c
  )
endif()
# aarch64 platform files
if(XE_TARGET_AARCH64)
  target_sources(libavutil PRIVATE
    ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aarch64/cpu.c
  )
  if(XE_HAS_GAS_ASM)
    # NEON init files reference symbols from the .S files, so both must
    # be included together. Requires a GAS-compatible assembler (gcc/clang).
    target_sources(libavutil PRIVATE
      ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aarch64/float_dsp_init.c
      ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aarch64/tx_float_init.c
    )
    xe_add_gas_sources(libavutil
      ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aarch64/float_dsp_neon.S
      ${XENIA_ROOT}/third_party/FFmpeg/libavutil/aarch64/tx_float_neon.S
    )
  endif()
endif()
ffmpeg_common(libavutil)

# --- libavcodec ---
add_library(libavcodec STATIC
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/ac3_parser.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/adts_parser.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/allcodecs.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/avcodec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/avdct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/packet.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/bitstream.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/bitstream_filters.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/bsf.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/codec_desc.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/codec_par.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/d3d11va.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/decode.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/dirac.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/dv_profile.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/encode.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/imgconvert.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/jni.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mathtables.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mediacodec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpeg12framerate.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/options.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/parser.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/parsers.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/profiles.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/qsv_api.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/raw.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/utils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/vorbis_parser.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/xiph.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/dct32_fixed.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/dct32_float.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/faandct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/faanidct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/fdctdsp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/jfdctfst.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/jfdctint.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/idctdsp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/simple_idct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/jrevdct.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudio.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodec_common.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodsp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodsp_data.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodsp_fixed.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodsp_float.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodecheader.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodata.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/sinewin.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/wma_freqs.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodec_fixed.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiodec_float.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/wmaprodec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/wma.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/wma_common.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/wmadec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aactab.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudio_parser.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/pthread.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/pthread_slice.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/pthread_frame.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/get_buffer.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/vlc.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/threadprogress.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/bsf/null.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/exif.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/mpegaudiotabs.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/tiff_common.c
)
if(WIN32)
  target_sources(libavcodec PRIVATE ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/file_open.c)
endif()
# x86 platform files
if(XE_TARGET_X86_64)
  target_sources(libavcodec PRIVATE
    ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/x86/constants.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/x86/fdctdsp_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/x86/idctdsp_init.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/x86/mpegaudiodsp.c
    ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/x86/fdct.c
  )
endif()
# aarch64 platform files
if(XE_TARGET_AARCH64)
  if(XE_HAS_GAS_ASM)
    target_sources(libavcodec PRIVATE
      ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aarch64/idctdsp_init_aarch64.c
      ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aarch64/mpegaudiodsp_init.c
    )
    xe_add_gas_sources(libavcodec
      ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aarch64/idctdsp_neon.S
      ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aarch64/mpegaudiodsp_neon.S
      ${XENIA_ROOT}/third_party/FFmpeg/libavcodec/aarch64/simple_idct_neon.S
    )
  endif()
endif()
target_include_directories(libavcodec PRIVATE
  ${XENIA_ROOT}/third_party/FFmpeg/libavcodec
)
target_link_libraries(libavcodec PRIVATE libavutil)
ffmpeg_common(libavcodec)

# --- libavformat ---
add_library(libavformat STATIC
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/allformats.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/avio.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/aviobuf.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/dump.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/format.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/id3v1.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/id3v2.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/metadata.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/mux.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/options.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/protocols.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/riff.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/sdp.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/url.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/utils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/riffdec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/asfdec_f.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/asf.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/asfcrypt.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/avlanguage.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/mp3dec.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/replaygain.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/file.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/demux.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/avformat.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/seek.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/demux_utils.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/urldecode.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/asf_tags.c
  ${XENIA_ROOT}/third_party/FFmpeg/libavformat/to_upper4.c
)
if(WIN32)
  target_sources(libavformat PRIVATE ${XENIA_ROOT}/third_party/FFmpeg/libavformat/file_open.c)
endif()
target_link_libraries(libavformat PRIVATE libavutil libavcodec)
ffmpeg_common(libavformat)
