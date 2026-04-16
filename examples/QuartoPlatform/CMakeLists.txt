include(FetchContent)

# hiredis
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.2.0
    GIT_SHALLOW    TRUE
)
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(hiredis)

# libcurl (optional — OAuth and K8s client need it at runtime)
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CURL QUIET libcurl)
    endif()
endif()
if(NOT CURL_FOUND)
    message(STATUS "libcurl not found — OAuth and K8s client will be compiled as stubs")
endif()

add_executable(quarto_platform
    src/main.cpp
    src/config.cpp
    src/gateway.cpp
    src/editor.cpp
    src/oauth.cpp
    src/k8s_client.cpp
    src/redis_client.cpp
    src/quarto.cpp
)

target_include_directories(quarto_platform PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(quarto_platform PRIVATE
    ServerWeb
    yaml-cpp::yaml-cpp
    hiredis::hiredis
    nlohmann_json::nlohmann_json
)

if(CURL_FOUND)
    target_include_directories(quarto_platform PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(quarto_platform PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(quarto_platform PRIVATE HAS_CURL=1)
endif()

if(SERVERCORE_BUILD_TESTS)
    add_executable(test_quarto_config tests/test_config.cpp src/config.cpp)
    target_include_directories(test_quarto_config PRIVATE src)
    target_link_libraries(test_quarto_config PRIVATE yaml-cpp::yaml-cpp GTest::gtest_main)
    add_test(NAME test_quarto_config COMMAND test_quarto_config)
endif()
