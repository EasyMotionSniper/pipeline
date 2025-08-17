cmake_minimum_required(VERSION 3.25)
project(mock_s3_parquet)

find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

add_executable(mock_s3_parquet demo.cpp)
target_link_libraries(mock_s3_parquet
  arrow_shared parquet_shared
)