anyx360_add_hle(x360_hle_test_objs ${CMAKE_CURRENT_SOURCE_DIR}/hle)

add_executable(test_hle hle/test_hle.cpp)
target_link_libraries(test_hle PRIVATE x360_hle_test_objs)
add_test(NAME hle COMMAND test_hle)

add_executable(test_dispatcher hle/test_dispatcher.cpp)
target_link_libraries(test_dispatcher PRIVATE x360_hle_test_objs)
add_test(NAME dispatcher COMMAND test_dispatcher)

# Python tools (synthetic inputs, no game data).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME find_helpers COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_find_helpers.py)
endif()
