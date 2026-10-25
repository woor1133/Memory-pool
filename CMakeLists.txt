# 指定 cmake 最低版本
cmake_minimum_required(VERSION 3.10)

# 指定 C++ 标准，不强制要求使用该标准
set(CMAKE_CXX_STANDARD 11)

# 项目名称
project(Memory-pool CXX)

# 指定源文件
FILE(GLOB SRC_FILES "src/*.cpp")
FILE(GLOB GLOB TEST_FILES "tests/*.cpp")

#添加可执行文件目标
add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${TEST_FILES}
)