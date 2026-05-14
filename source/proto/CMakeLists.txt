set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)

find_package(Protobuf REQUIRED)

set(PROTO_FILES ErpMessage.proto)

add_library(indyproto ${PROTO_FILES})
target_include_directories(indyproto INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(indyproto PUBLIC protobuf::libprotobuf)

protobuf_generate(TARGET indyproto LANGUAGE cpp)
