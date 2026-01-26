cmake_minimum_required(VERSION 3.14)

# 1. 项目名称
project(HttpServerProject CXX)

# 2. 设置 C++ 标准 (推荐 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 3. 集成 nlohmann/json
# [优化] 关闭 json 库自带的测试构建，否则编译时间会很长
set(JSON_BuildTests OFF CACHE INTERNAL "")

#添加子目录
# 告诉 CMake：去 "./json" 文件夹里找 CMakeLists.txt 并运行它
add_subdirectory(json)

# 4. 定义你的可执行文件
# 自动查找当前目录下的所有 .cpp 和 .h 文件
file(GLOB SOURCES "src/*.cpp" "src/*.h")

# 创建可执行文件
add_executable(server ${SOURCES})

# 5. 链接库
# 查找线程库 (pthread)
find_package(Threads REQUIRED)

#链接 nlohmann_json 和 线程库只要链接了 nlohmann_json::nlohmann_json,CMake 会自动处理头文件路径，不需要手动设置
target_link_libraries(server PRIVATE 
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    message(WARNING "代码包含 Linux Epoll，确保连接到 Linux 远程服务器进行编译，否则 MSVC 编译器无法通过。")
endif()