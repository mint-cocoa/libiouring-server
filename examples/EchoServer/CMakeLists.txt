add_executable(EchoServer src/main.cpp)
target_include_directories(EchoServer PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/benchmarks/common
)
target_link_libraries(EchoServer PRIVATE ServerCore)
target_compile_features(EchoServer PRIVATE cxx_std_23)
