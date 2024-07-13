cmake_minimum_required(VERSION 3.16)
project(varint_bench LANGUAGES CXX)

include(CPM.cmake)
# find_package(benchmark REQUIRED)
CPMAddPackage(
    NAME benchmark
    GITHUB_REPOSITORY google/benchmark
    VERSION 1.8.3
    OPTIONS
    "BENCHMARK_ENABLE_TESTING OFF"
    "BENCHMARK_ENABLE_INSTALL OFF"
  )
CPMAddPackage("gh:boost-ext/ut@2.0.1")
set(CMAKE_CXX_STANDARD 23)

add_executable(num_varints_bench num_varints_bench.cpp)
target_compile_definitions(num_varints_bench PRIVATE PARALLEL)
target_compile_options(num_varints_bench PRIVATE -march=native)
target_include_directories(num_varints_bench PRIVATE ${benchmark_SOURCE_DIR}/include)
target_link_libraries(num_varints_bench PRIVATE benchmark::benchmark_main)

add_executable(parse_varint_bench parse_varint_bench.cpp)
target_compile_options(parse_varint_bench PRIVATE -march=native)
target_include_directories(parse_varint_bench PRIVATE ${benchmark_SOURCE_DIR}/include)
target_link_libraries(parse_varint_bench PRIVATE benchmark::benchmark_main)


add_executable(unittest test.cpp)
target_link_libraries(unittest PRIVATE Boost::ut)

enable_testing()

add_test(NAME unittest COMMAND unittest)