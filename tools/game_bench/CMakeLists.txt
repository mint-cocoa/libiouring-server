find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/examples/GameServer/proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    "${PROTO_DIR}/Enum.proto"
    "${PROTO_DIR}/Common.proto"
    "${PROTO_DIR}/Auth.proto"
    "${PROTO_DIR}/Game.proto"
)

add_executable(game_bench
    game_bench.cpp
    BenchClient.cpp
    ${PROTO_SRCS}
)

target_include_directories(game_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(game_bench PRIVATE
    PkgConfig::URING
    protobuf::libprotobuf
    Threads::Threads
)

target_compile_features(game_bench PRIVATE cxx_std_20)
