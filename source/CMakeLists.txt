file(GLOB_RECURSE SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# Exclude any source files with a _main.cpp suffix
list(FILTER SOURCES EXCLUDE REGEX ".*_main.cpp")

set(HEADERS
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools"
  "${CMAKE_CURRENT_SOURCE_DIR}/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/net"
)

include(FetchContent)

# IXWebSocket options
set(USE_TLS ON)

FetchContent_Declare(
  IXWebSocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
)

FetchContent_MakeAvailable(IXWebSocket)
find_package(PkgConfig REQUIRED)
pkg_check_modules(notcurses REQUIRED IMPORTED_TARGET notcurses)
pkg_check_modules(notcurses++ REQUIRED IMPORTED_TARGET notcurses++)

add_subdirectory(proto)

add_library(${PROJECT_LIB} ${SOURCES})
target_include_directories(${PROJECT_LIB} PUBLIC ${HEADERS})
target_link_libraries(${PROJECT_LIB} PUBLIC indyproto ixwebsocket PkgConfig::notcurses PkgConfig::notcurses++)

if(BUILD_MAIN)
  add_executable(${PROJECT_EXEC} ${SOURCES} client_main.cpp)
  target_link_libraries(${PROJECT_EXEC} PUBLIC ${PROJECT_LIB})
  target_compile_definitions(${PROJECT_EXEC} PRIVATE cxx_std_23)
  set_target_properties(${PROJECT_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()

if(BUILD_REPLAY_SERVER)
  add_executable(${REPLAY_SERVER_EXEC} ${SOURCES} replay_server_main.cpp)
  target_link_libraries(${REPLAY_SERVER_EXEC} PUBLIC ${PROJECT_LIB})
  target_compile_definitions(${REPLAY_SERVER_EXEC} PRIVATE cxx_std_23)
  set_target_properties(${REPLAY_SERVER_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()

if(BUILD_DISP_DEMO)
  add_executable(${DISP_DEMO_EXEC} ${SOURCES} demo_display_main.cpp)
  target_link_libraries(${DISP_DEMO_EXEC} PUBLIC ${PROJECT_LIB})
  target_compile_definitions(${DISP_DEMO_EXEC} PRIVATE cxx_std_23)
  set_target_properties(${DISP_DEMO_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()
