cmake_minimum_required(VERSION 3.20)
project(logger_core_tests LANGUAGES C)

include(FetchContent)
if(NOT UNITY_SOURCE_DIR)
  FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG cbcd08fa7de711053a3deec6339ee89cad5d2697
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(unity)
  set(UNITY_SOURCE_DIR "${unity_SOURCE_DIR}")
endif()

add_executable(logger_core_tests
  test_core.c
  ../app/src/record.c
  ../app/src/state.c
  ../app/src/timekeeping.c
  "${UNITY_SOURCE_DIR}/src/unity.c"
)
target_include_directories(logger_core_tests PRIVATE ../app/include "${UNITY_SOURCE_DIR}/src")
target_compile_definitions(logger_core_tests PRIVATE UNITY_INCLUDE_CONFIG_H)
target_include_directories(logger_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(logger_core_tests PRIVATE c_std_11)
target_compile_options(logger_core_tests PRIVATE -Wall -Wextra -Werror -pedantic)
enable_testing()
add_test(NAME logger_core COMMAND logger_core_tests)
