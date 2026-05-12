# 1. 稍微提升一点最低版本，以更好支持 VS 的远程开发特性
cmake_minimum_required(VERSION 3.10)
PROJECT(tc_http_server)

# 2. 收集各目录下的源文件
AUX_SOURCE_DIRECTORY(./ SRC_LIST)
AUX_SOURCE_DIRECTORY(./common COMMON_LIST)
#AUX_SOURCE_DIRECTORY(./jsoncpp JSON_LIST)
AUX_SOURCE_DIRECTORY(./mysql MYSQL_LIST)
AUX_SOURCE_DIRECTORY(./redis REDIS_LIST)
AUX_SOURCE_DIRECTORY(./base BASE_LIST)
AUX_SOURCE_DIRECTORY(./api API_LIST)
AUX_SOURCE_DIRECTORY(./log LOG_LIST)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-g -W -Wall -std=c++14)
    add_definitions(
        -D_REENTRANT -D_FILE_OFFSET_BITS=64 
        -DAC_HAS_INFO -DAC_HAS_WARNING -DAC_HAS_ERROR -DAC_HAS_CRITICAL 
        -DTIXML_USE_STL -DAC_HAS_DEBUG -DLINUX_DAEMON
    )
endif()

# 3. 指定头文件包含目录
INCLUDE_DIRECTORIES(
    /usr/include/fastdfs 
    /usr/include/fastcommon 
    /usr/local/include/hiredis 
    /usr/include/mysql
)
INCLUDE_DIRECTORIES(
    ./ ./include ./mysql ./redis ./base ./api ./log
)


ADD_EXECUTABLE(tc_http_server  
    ${BASE_LIST} ${API_LIST} ${LOG_LIST}
    ${MYSQL_LIST} ${REDIS_LIST} 
    ${SRC_LIST} ${COMMON_LIST}
    "index.html"
)

# 4. 链接所需的动态库
TARGET_LINK_LIBRARIES(tc_http_server pthread fdfsclient fastcommon m mysqlclient curl jsoncpp faiss) 


set(HTML_SRC "${CMAKE_CURRENT_SOURCE_DIR}/index.html")
set(HTML_DST "/home/boyu/jieluote/index.html") # 根据实际需要修改 路径

add_custom_command(TARGET tc_http_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Auto deploying index.html to run directory..."
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HTML_SRC} ${HTML_DST}
    COMMENT "Deploying HTML file to run directory"
)
