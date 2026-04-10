cmake_minimum_required(VERSION 3.22)
find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/examples/GameServer/proto")
file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_executable(GameServerEpoll
    src/main.cpp
    src/epoll_worker.cpp
    src/epoll_connection.cpp
    src/tcp_listener.cpp
    src/zone.cpp
    src/zone_manager.cpp
    ${PROTO_SRCS}
)

target_include_directories(GameServerEpoll PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/examples/benchmarks/common
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(GameServerEpoll PRIVATE
    Threads::Threads
    protobuf::libprotobuf
)

target_compile_features(GameServerEpoll PRIVATE cxx_std_23)
