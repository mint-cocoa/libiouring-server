include(GoogleTest)

add_executable(voxel_server_tests
    test_voxel_world.cpp
    test_collision.cpp
    test_streaming.cpp
    test_game_match.cpp
)

target_link_libraries(voxel_server_tests
    PRIVATE voxel_server GTest::gtest_main
)

gtest_discover_tests(voxel_server_tests)
