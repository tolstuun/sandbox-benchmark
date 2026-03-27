cmake_minimum_required(VERSION 3.20)
project(SandboxBenchmarkRunner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SandboxBenchmarkRunner
    src/main.cpp
)

target_include_directories(SandboxBenchmarkRunner PRIVATE include)
