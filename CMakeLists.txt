cmake_minimum_required(VERSION 3.14)
project(SPSCLab_Combined LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -pthread -Wall -Wextra -Wno-interference-size")

find_package(benchmark REQUIRED)

include_directories(include)

add_executable(spsc_bench benchmarks/main.cpp)
target_link_libraries(spsc_bench PRIVATE benchmark::benchmark pthread)
