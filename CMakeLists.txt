cmake_minimum_required(VERSION 3.8)
project(indy-cpp CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(BUILD_MAIN ON CACHE BOOL "")
set(BUILD_REPLAY_SERVER ON CACHE BOOL "")
set(BUILD_TESTS OFF "")
set(CMAKE_BUILD_TYPE Debug)

set(PROJECT_EXEC indycpp)
set(REPLAY_SERVER_EXEC indyreplay)
set(TESTS_EXEC indytests)
set(EXEC_OUT "${CMAKE_CURRENT_SOURCE_DIR}/build/")


add_subdirectory(source)

#####

# add_subdirectory(proto)
# add_executable(indycpp source/main.cpp)

# target_compile_definitions(indycpp PRIVATE cxx_std_20)
# target_link_libraries(indycpp PUBLIC indyproto)

#####


# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)

# find_package(Protobuf REQUIRED)
# protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/ErpMessage.proto)

# add_executable(my_app main.cpp ${PROTO_SRCS} ${PROTO_HDRS})
# target_compile_definitions(my_app PRIVATE cxx_std_11)
# target_link_libraries(my_app PRIVATE protobuf::libprotobuf)

# Don't love but bc I'm using a mega old distro :(
# set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)

# find_package(Protobuf REQUIRED)

# add_executable(project_executable main.cpp)
