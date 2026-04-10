cmake_minimum_required(VERSION 3.22)

add_executable(WebServer
    src/main.cpp
    src/db/pg_db.cpp
    src/handler/item_handler.cpp
    src/handler/health_handler.cpp
)

target_include_directories(WebServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(WebServer PRIVATE
    ServerWeb
    ServerStorage
    nlohmann_json::nlohmann_json
)

target_compile_features(WebServer PRIVATE cxx_std_23)
