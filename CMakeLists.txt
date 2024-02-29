cmake_minimum_required(VERSION 3.16)
project(varint_bench LANGUAGES CXX)

include(CPM.cmake)
find_package(benchmark REQUIRED)
CPMAddPackage("gh:boost-ext/ut@2.0.1")
set(CMAKE_CXX_STANDARD 20)

add_executable(varint_bench bench.cpp)
target_include_directories(varint_bench PRIVATE ${benchmark_SOURCE_DIR}/include)
target_link_libraries(varint_bench PRIVATE benchmark::benchmark_main)

add_executable(unittest test.cpp)
target_link_libraries(unittest PRIVATE Boost::ut)

enable_testing()

add_test(NAME unittest COMMAND unittest)