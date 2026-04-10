cmake_minimum_required(VERSION 3.22)

find_package(Protobuf REQUIRED)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_executable(GameServer
    src/main.cpp
    src/net/packet_session.cpp
    src/net/io_worker.cpp
    src/net/io_worker_pool.cpp
    src/game/player_context.cpp
    src/net/game_session.cpp
    src/game/room.cpp
    src/game/room_manager.cpp
    src/game/dungeon_generator.cpp
    src/game/bot_manager.cpp
    src/game/projectile_manager.cpp
    src/handler/auth_handler.cpp
    src/handler/char_handler.cpp
    src/handler/room_handler.cpp
    src/handler/game_handler.cpp
    src/system/combat_system.cpp
    src/system/chat_system.cpp
    src/system/party_system.cpp
    src/handler/social_handler.cpp
    src/db/memory_db_service.cpp
    src/db/sqlite_db_service.cpp
    src/db/pg_db_service.cpp
    vendor/sqlite3.c
    ${PROTO_SRCS}
)

target_include_directories(GameServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(GameServer PRIVATE
    ServerCore
    ServerStorage
    protobuf::libprotobuf
)

target_compile_features(GameServer PRIVATE cxx_std_23)
