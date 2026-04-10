add_executable(echo_bench echo_bench.cpp)
target_link_libraries(echo_bench PRIVATE Threads::Threads)

add_subdirectory(game_bench)
