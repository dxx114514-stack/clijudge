cmake_minimum_required(VERSION 3.10)
project(CliJudge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加include目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 添加源文�?add_executable(clijudge src/main.cpp)

# 链接Windows�?target_link_libraries(clijudge psapi userenv ole32)

# 设置Windows版本
target_compile_definitions(clijudge PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
