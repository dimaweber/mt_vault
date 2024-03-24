cmake_minimum_required(VERSION 3.27)
project(mt_vault)

set(CMAKE_CXX_STANDARD 20)
set(TARGET_LIB mt_vault)
set(TARGET_UNITTEST mt_vault.unittest)
set(TARGET_BENCHMARK mt_vault.benchmark)

find_package(fmt  REQUIRED)
find_package(benchmark  REQUIRED)
find_package(GTest)

#add_library(${TARGET_LIB} mt_vault.h)
add_executable(${TARGET_UNITTEST} mt_vault.unittest.cpp)
add_executable(${TARGET_BENCHMARK} mt_vault.benchmark.cpp)

enable_testing()
include(GoogleTest)
gtest_discover_tests(${TARGET_UNITTEST})

target_link_libraries(${TARGET_BENCHMARK} benchmark::benchmark_main fmt::fmt)
target_link_libraries(${TARGET_UNITTEST} GTest::gtest_main fmt::fmt)
