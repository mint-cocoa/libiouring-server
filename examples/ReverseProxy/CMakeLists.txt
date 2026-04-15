cmake_minimum_required(VERSION 3.22)

add_executable(ReverseProxyExample src/main.cpp)

target_link_libraries(ReverseProxyExample PRIVATE ServerWeb)

target_compile_features(ReverseProxyExample PRIVATE cxx_std_23)
