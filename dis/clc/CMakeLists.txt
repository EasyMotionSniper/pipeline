cmake_minimum_required(VERSION 3.16)
project(OrderReader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/s3_kv_store.cpp
    src/order_reader.cpp
    src/disk_cache_kv_store.cpp
    src/sliced_kv_store.cpp
    src/optimized_cache_kv_store.cpp
)

# Main library
add_library(order_reader_lib ${SOURCES})
target_link_libraries(order_reader_lib
    Arrow::arrow_shared
    Parquet::parquet_shared
    ${AWSSDK_LINK_LIBRARIES}
    Threads::Threads
)

# Test executable
add_executable(order_reader_test
    test/test_main.cpp
    test/test_s3_kv_store.cpp
    test/test_order_reader.cpp
    test/test_disk_cache.cpp
    test/test_sliced_store.cpp
)
target_link_libraries(order_reader_test
    order_reader_lib
    GTest::GTest
    GTest::Main
)

# Benchmark executable
add_executable(order_reader_benchmark
    benchmark/benchmark_main.cpp
)
target_link_libraries(order_reader_benchmark
    order_reader_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Enable testing
enable_testing()
add_test(NAME order_reader_test COMMAND order_reader_test)