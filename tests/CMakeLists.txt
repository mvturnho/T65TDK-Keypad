cmake_minimum_required(VERSION 3.16)
project(t65_keypad_native_tests LANGUAGES CXX)

enable_testing()

add_executable(test_keypad_state test_keypad_state.cpp)
target_compile_features(test_keypad_state PRIVATE cxx_std_17)
target_compile_options(test_keypad_state PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_include_directories(
  test_keypad_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../components/t65_keypad
)

add_test(NAME keypad_state COMMAND test_keypad_state)
