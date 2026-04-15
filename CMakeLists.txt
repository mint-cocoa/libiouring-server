cmake_minimum_required(VERSION 3.16)
project(ServerCore VERSION 4.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Dependencies ----

include(FetchContent)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
)
FetchContent_MakeAvailable(spdlog)

# GLM (header-only math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# liburing
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)

# Threads
find_package(Threads REQUIRED)

# Tracy Profiler (optional)
option(SERVERCORE_TRACY "Enable Tracy Profiler" OFF)

if(SERVERCORE_TRACY)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.13.1
        GIT_SHALLOW    TRUE
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
endif()

# Protobuf (optional for now, needed for protocol layer)
find_package(Protobuf QUIET)

# --- ServerCore Library ----

add_library(ServerCore STATIC
    # Ring (io_uring wrapper)
    src/ring/RingBuffer.cpp
    src/ring/IoRing.cpp
    # IO layer (Session, Listener)
    src/io/Session.cpp
    src/io/Listener.cpp
    # Job system (GlobalQueue, JobQueue, JobTimer)
    src/job/GlobalQueue.cpp
    src/job/JobQueue.cpp
    src/job/JobTimer.cpp
)

target_include_directories(ServerCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ServerCore
    PUBLIC
        PkgConfig::URING
        Threads::Threads
        spdlog::spdlog
)

if(SERVERCORE_TRACY)
    target_link_libraries(ServerCore PUBLIC Tracy::TracyClient)
endif()

target_compile_options(ServerCore
    PRIVATE
        -Wall -Wextra -Wpedantic
)

# --- Examples (core-only) ----

add_subdirectory(examples/GameServer)
add_subdirectory(examples/WebServer)
add_subdirectory(examples/EchoServer)
add_subdirectory(examples/DX12DemoServer)
add_subdirectory(examples/BattleServer)
add_subdirectory(examples/AntBattleServer)
add_subdirectory(examples/BoidServer)
add_subdirectory(examples/benchmarks/GameServerIntegrated)
add_subdirectory(examples/benchmarks/GameServerSeparated)
add_subdirectory(examples/benchmarks/GameServerEpoll)
add_subdirectory(tools)

# --- ServerStorage (PostgreSQL worker-pool storage) ----

pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

add_library(ServerStorage STATIC
    src/storage/PgResult.cpp
    src/storage/PgConnection.cpp
    src/storage/PgWorkerPool.cpp
    src/storage/PgConnectionPool.cpp
)

target_include_directories(ServerStorage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ServerStorage
    PUBLIC ServerCore
    PRIVATE PkgConfig::LIBPQ
)

target_compile_options(ServerStorage PRIVATE -Wall -Wextra -Wpedantic)

# --- ServerWeb (HTTP framework) ----

# llhttp — Node.js HTTP parser
FetchContent_Declare(
    llhttp
    GIT_REPOSITORY https://github.com/nodejs/llhttp.git
    GIT_TAG        release/v9.3.1
    GIT_SHALLOW    TRUE
)
set(LLHTTP_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(LLHTTP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(llhttp)

find_package(OpenSSL REQUIRED)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_library(ServerWeb STATIC
    src/web/HttpParser.cpp
    src/web/HttpResponse.cpp
    src/web/HttpSession.cpp
    src/web/Middleware.cpp
    src/web/RadixTree.cpp
    src/web/RouteGroup.cpp
    src/web/Router.cpp
    src/web/WebServer.cpp
    src/web/UpstreamSession.cpp
    src/web/middleware/Cors.cpp
    src/web/middleware/StaticFiles.cpp
    src/web/ws/WsFrameParser.cpp
    src/web/ws/WsHandshake.cpp
)

target_include_directories(ServerWeb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ServerWeb
    PUBLIC ServerCore
    PRIVATE llhttp_static OpenSSL::Crypto nlohmann_json::nlohmann_json
)

target_compile_options(ServerWeb PRIVATE -Wall -Wextra -Wpedantic)

# Temporarily disabled — needs PlayerId/IoCommand self-definition (post-refactoring)
# # --- ServerGame (Presence, Matchmaker, Match, Notifications) ---
#
# add_library(ServerGame STATIC
#     src/game/WsFrameBuilder.cpp
#     src/game/PlayerRegistry.cpp
#     src/game/presence/PresenceTracker.cpp
#     src/game/matchmaker/QueryParser.cpp
#     src/game/matchmaker/MatchmakerQueue.cpp
#     src/game/match/Match.cpp
#     src/game/match/MatchRegistry.cpp
#     src/game/notification/NotificationService.cpp
# )
#
# target_include_directories(ServerGame
#     PUBLIC
#         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#         $<INSTALL_INTERFACE:include>
# )
#
# target_link_libraries(ServerGame
#     PUBLIC ServerCore
#     PRIVATE nlohmann_json::nlohmann_json
# )
#
# target_compile_options(ServerGame PRIVATE -Wall -Wextra -Wpedantic)

# --- Tests ----

option(SERVERCORE_BUILD_TESTS "Build tests" ON)

# --- Sanitizers ----
# Usage: cmake -DSERVERCORE_SANITIZER=tsan ..
#   Options: tsan, asan, ubsan, asan+ubsan, off (default)
# Note: TSan and ASan are mutually exclusive — use separate build dirs.

set(SERVERCORE_SANITIZER "off" CACHE STRING "Sanitizer to enable (tsan|asan|ubsan|asan+ubsan|off)")

if(SERVERCORE_TRACY AND NOT SERVERCORE_SANITIZER STREQUAL "off")
    message(WARNING "Tracy + Sanitizer enabled simultaneously. "
                    "Tracy's lock instrumentation may cause false positives with TSan. "
                    "Consider using separate build directories.")
endif()

if(NOT SERVERCORE_SANITIZER STREQUAL "off")
    set(SAN_COMMON_FLAGS -fno-omit-frame-pointer -g)

    if(SERVERCORE_SANITIZER STREQUAL "tsan")
        list(APPEND SAN_COMMON_FLAGS -fsanitize=thread)
    elseif(SERVERCORE_SANITIZER STREQUAL "asan")
        list(APPEND SAN_COMMON_FLAGS -fsanitize=address)
    elseif(SERVERCORE_SANITIZER STREQUAL "ubsan")
        list(APPEND SAN_COMMON_FLAGS -fsanitize=undefined)
    elseif(SERVERCORE_SANITIZER STREQUAL "asan+ubsan")
        list(APPEND SAN_COMMON_FLAGS -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "Unknown sanitizer: ${SERVERCORE_SANITIZER}")
    endif()

    add_compile_options(${SAN_COMMON_FLAGS})
    add_link_options(${SAN_COMMON_FLAGS})
    message(STATUS "Sanitizer enabled: ${SERVERCORE_SANITIZER}")
endif()

if(SERVERCORE_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.0
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
