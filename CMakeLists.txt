cmake_minimum_required(VERSION 4.2.3)
project(indy-cpp CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

set(BUILD_MAIN ON CACHE BOOL "")
set(BUILD_REPLAY_SERVER OFF CACHE BOOL "")
set(BUILD_TESTS OFF CACHE BOOL "")
set(BUILD_DISP_DEMO OFF CACHE BOOL "")

set(CMAKE_BUILD_TYPE Debug)

set(PROJECT_LIB indylib)
set(PROJECT_EXEC indycpp)
set(REPLAY_SERVER_EXEC indyreplay)
set(TESTS_EXEC indytests)
set(DISP_DEMO_EXEC demo-display)

set(EXEC_OUT "${CMAKE_CURRENT_SOURCE_DIR}/build/")

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/")

add_subdirectory(${SOURCE_DIR})


# Finally, *always* copy the compile_commands up one
execute_process(
  COMMAND cp compile_commands.json ../
  WORKING_DIRECTORY ${EXEC_OUT}
)
