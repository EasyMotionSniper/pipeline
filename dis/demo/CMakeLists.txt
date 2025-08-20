cmake_minimum_required(VERSION 3.25)
project(mock_s3_parquet)

find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

# add_executable(mock_s3_parquet demo.cpp)
# target_link_libraries(mock_s3_parquet
#   arrow_shared parquet_shared
# )


# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含FetchContent模块
include(FetchContent)

# 方法1: 使用Git仓库下载TLX (推荐)
FetchContent_Declare(
    tlx
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tlx
)

# 设置TLX的编译选项 (可选)
set(TLX_BUILD_TESTS OFF CACHE BOOL "Build TLX tests")
set(TLX_BUILD_EXAMPLES OFF CACHE BOOL "Build TLX examples")

# 获取并构建TLX
FetchContent_MakeAvailable(tlx)



# 创建你的可执行文件
add_executable(de 
    my.cpp
)

target_link_libraries(de PRIVATE tlx)



