cmake_minimum_required(VERSION 3.22)
find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/examples/GameServer/proto")
file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_executable(GameServerIntegrated
    src/main.cpp
    src/bench_session.cpp
    src/zone.cpp
    src/zone_manager.cpp
    ${PROTO_SRCS}
)

target_include_directories(GameServerIntegrated PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples/benchmarks/common
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(GameServerIntegrated PRIVATE
    ServerCore
    protobuf::libprotobuf
)

target_compile_features(GameServerIntegrated PRIVATE cxx_std_23)
