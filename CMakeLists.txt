cmake_minimum_required(VERSION 3.10)
project(CliJudge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include 目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 单翻译单元构建：miniz 通过 include/miniz_impl.h 随 main.cpp 一并编译
add_executable(clijudge src/main.cpp)

if(WIN32)
  target_compile_definitions(clijudge PRIVATE WINVER=0x0600 _WIN32_WINNT=0x0600)
  target_link_libraries(clijudge psapi userenv ole32 winhttp)
endif()
