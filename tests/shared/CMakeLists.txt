include(GoogleTest)

add_executable(voxel_shared_tests
    test_byte_buffer.cpp
    test_protocol.cpp
    test_svo.cpp
    test_sdf.cpp
)

target_link_libraries(voxel_shared_tests
    PRIVATE voxel_shared GTest::gtest_main
)

gtest_discover_tests(voxel_shared_tests)
