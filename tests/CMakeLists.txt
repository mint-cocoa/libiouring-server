# --- ServerCore tests ---

add_executable(test_mpsc_queue core/test_mpsc_queue.cpp)
target_link_libraries(test_mpsc_queue PRIVATE ServerCore GTest::gtest_main)
add_test(NAME test_mpsc_queue COMMAND test_mpsc_queue)

add_executable(test_session_lifecycle io/test_session_lifecycle.cpp)
target_link_libraries(test_session_lifecycle PRIVATE ServerCore GTest::gtest_main)
add_test(NAME test_session_lifecycle COMMAND test_session_lifecycle)

add_executable(test_cross_thread_flow io/test_cross_thread_flow.cpp)
target_link_libraries(test_cross_thread_flow PRIVATE ServerCore GTest::gtest_main)
add_test(NAME test_cross_thread_flow COMMAND test_cross_thread_flow)

add_executable(test_job_queue job/test_job_queue.cpp)
target_link_libraries(test_job_queue PRIVATE ServerCore GTest::gtest_main)
add_test(NAME test_job_queue COMMAND test_job_queue)

add_executable(test_poll_add ring/test_poll_add.cpp)
target_link_libraries(test_poll_add PRIVATE ServerCore GTest::gtest_main)
add_test(NAME test_poll_add COMMAND test_poll_add)

# --- ServerStorage tests ---

add_executable(test_pg_result storage/test_pg_result.cpp)
target_link_libraries(test_pg_result PRIVATE ServerStorage GTest::gtest_main)
add_test(NAME test_pg_result COMMAND test_pg_result)

# --- Temporarily disabled (depend on removed framework/targets) ---
# ServerWeb tests — depend on ServerWeb target (disabled)
# ServerGame tests — depend on ServerGame target (disabled)
# VoxelArena tests — depend on ServerGame + voxel_shared (disabled)
