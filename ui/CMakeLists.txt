# Drawing code shared by the launcher, x360tool and the runtime's Kinect window.
#
# ui_wic: pictures through the Windows Imaging Component (decoding any format, box art's front
# cover, JPEG output). It needs COM, so it stays out of the game process: the launcher and
# x360tool use it.
add_library(ui_wic STATIC wic/image.cpp)
target_include_directories(ui_wic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ui_wic PRIVATE UNICODE _UNICODE)
target_link_libraries(ui_wic PUBLIC windowscodecs ole32)
