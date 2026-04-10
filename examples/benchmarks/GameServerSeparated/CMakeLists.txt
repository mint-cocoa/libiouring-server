cmake_minimum_required(VERSION 3.22)
find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/examples/GameServer/proto")
file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_executable(GameServerSeparated
    src/main.cpp
    src/bench_session.cpp
    src/zone.cpp
    src/zone_manager.cpp
    src/zone_worker.cpp
    ${PROTO_SRCS}
)

target_include_directories(GameServerSeparated PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples/benchmarks/common
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(GameServerSeparated PRIVATE
    ServerCore
    protobuf::libprotobuf
)

target_compile_features(GameServerSeparated PRIVATE cxx_std_23)
