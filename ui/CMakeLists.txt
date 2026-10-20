# Drawing code shared by the launcher, x360tool and the runtime's Kinect window.
#
# ui_wic: pictures through the Windows Imaging Component (decoding any format, box art's front
# cover, JPEG output). It needs COM, so it stays out of the game process: the launcher and
# x360tool use it.
add_library(ui_wic STATIC wic/image.cpp)
target_include_directories(ui_wic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ui_wic PRIVATE UNICODE _UNICODE)
target_link_libraries(ui_wic PUBLIC windowscodecs ole32)

# ui_d2d: drawing with Direct2D and DirectWrite (a canvas over a window or a bitmap, text with fonts
# given from memory, pictures from BGRA). No COM apartment needed: the game process can use it too
# (the Kinect debug window).
add_library(ui_d2d STATIC d2d/canvas.cpp)
target_include_directories(ui_d2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ui_d2d PRIVATE UNICODE _UNICODE)
target_link_libraries(ui_d2d PUBLIC d2d1 dwrite)
