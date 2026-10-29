anyx360_add_hle(x360_hle_test_objs ${CMAKE_CURRENT_SOURCE_DIR}/hle)

add_executable(test_hle hle/test_hle.cpp)
target_link_libraries(test_hle PRIVATE x360_hle_test_objs)
add_test(NAME hle COMMAND test_hle)

# Avatars on a made-up avatar asset pack: descriptions, profile.json, the editor's rules.
add_executable(test_avatar avatar/test_avatar.cpp)
target_link_libraries(test_avatar PRIVATE x360_formats)
target_compile_features(test_avatar PRIVATE cxx_std_20)
add_test(NAME avatar COMMAND test_avatar)

add_executable(test_dispatcher hle/test_dispatcher.cpp)
target_link_libraries(test_dispatcher PRIVATE x360_hle_test_objs)
add_test(NAME dispatcher COMMAND test_dispatcher)

add_executable(test_title_update hle/test_title_update.cpp)
target_link_libraries(test_title_update PRIVATE x360_hle_test_objs)
add_test(NAME title_update COMMAND test_title_update)

add_executable(test_guide hle/test_guide.cpp)
target_link_libraries(test_guide PRIVATE x360_hle_test_objs)
add_test(NAME guide COMMAND test_guide)

add_executable(test_nui nui/test_nui.cpp)
target_link_libraries(test_nui PRIVATE x360_hle_test_objs)
add_test(NAME nui COMMAND test_nui)

add_executable(test_nui_bridge nui/test_bridge.cpp)
target_link_libraries(test_nui_bridge PRIVATE x360_hle_test_objs)
add_test(NAME nui_bridge COMMAND test_nui_bridge)

# Python tools (synthetic inputs, no game data).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME find_helpers COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_find_helpers.py)
endif()

# The pose bridge's tests need its packages: they run once tools/pose_bridge/build_bridge.ps1 has
# made the venv (reconfigure after it).
set(POSE_BRIDGE_PYTHON ${CMAKE_BINARY_DIR}/pose_bridge/venv/Scripts/python.exe)
if(EXISTS ${POSE_BRIDGE_PYTHON})
  add_test(NAME pose_bridge COMMAND ${POSE_BRIDGE_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/pose_bridge/run_tests.py)
endif()
