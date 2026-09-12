cmake_minimum_required(VERSION 3.10)
project(JudgeLite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加include目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 添加源文件
add_executable(judgelite src/main.cpp)

# 链接Windows库
target_link_libraries(judgelite psapi userenv ole32)

# 设置Windows版本
target_compile_definitions(judgelite PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)