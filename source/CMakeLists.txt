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

if(BUILD_MAIN)
  add_executable(${PROJECT_EXEC} ${SOURCES} client_main.cpp)

  set(Boost_USE_STATIC_LIBS OFF)
  set(Boost_USE_MULTITHREADED ON)
  set(Boost_USE_STATIC_RUNTIME OFF)

  include_directories(${Boost_INCLUDE_DIRS})

  target_compile_definitions(${PROJECT_EXEC} PRIVATE cxx_std_23)
  target_link_libraries(${PROJECT_EXEC} PUBLIC
    indyproto
    ${Boost_LIBRARIES}
    ixwebsocket PkgConfig::notcurses
    PkgConfig::notcurses++
  )
  target_include_directories(${PROJECT_EXEC} PUBLIC ${HEADERS})
  set_target_properties(${PROJECT_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()

if(BUILD_REPLAY_SERVER)
  add_executable(${REPLAY_SERVER_EXEC} ${SOURCES} replay_server_main.cpp)
  target_compile_definitions(${REPLAY_SERVER_EXEC} PRIVATE cxx_std_23)
  target_link_libraries(${REPLAY_SERVER_EXEC} PUBLIC indyproto ixwebsocket)
  target_include_directories(${REPLAY_SERVER_EXEC} PUBLIC ${HEADERS})
  set_target_properties(${REPLAY_SERVER_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()

if(BUILD_DISP_DEMO)
  
  add_executable(${DISP_DEMO_EXEC} ${SOURCES} demo_display_main.cpp)
  target_compile_definitions(${DISP_DEMO_EXEC} PRIVATE cxx_std_23)
  target_link_libraries(${DISP_DEMO_EXEC} PUBLIC indyproto ixwebsocket PkgConfig::notcurses PkgConfig::notcurses++)
  target_include_directories(${DISP_DEMO_EXEC} PUBLIC ${HEADERS})
  set_target_properties(${DISP_DEMO_EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXEC_OUT})
endif()

# set_target_properties(${PROJECT_EXEC} PROPERTIES COMPILE_FLAGS "-save-temps -fverbose-asm -O1 -Wa,-adhlmn=main.lst")

# add_executable(indycpp ${SOURCES})
# target_link_libraries(indycpp PRIVATE )
