cmake_minimum_required(VERSION 3.20)

project(anvil_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(anvil-tools
    src/anvil-tools.cpp
)

target_compile_features(anvil-tools PRIVATE cxx_std_17)
