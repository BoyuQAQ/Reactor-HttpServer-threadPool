#!/bin/bash
set -e

echo "=== Backend 启动脚本 ==="

# 等待服务函数
wait_for_service() {
    local host=$1
    local port=$2
    local name=$3
    local max_attempts=30
    local attempt=1

    echo "等待 $name ($host:$port) 就绪..."
    while [ $attempt -le $max_attempts ]; do
        if nc -z "$host" "$port" 2>/dev/null; then
            echo "$name 已就绪"
            return 0
        fi
        echo "尝试 $attempt/$max_attempts: $name 未就绪..."
        sleep 2
        attempt=$((attempt + 1))
    done
    echo "警告: $name 超过最大等待时间"
    return 0
}

# 等待依赖服务
wait_for_service "mysql" 3306 "MySQL"
wait_for_service "redis" 6379 "Redis"

# 动态替换配置文件中的服务地址
echo "配置服务地址..."

# 修改 FastDFS 配置
if [ -f /app/conf/client.conf ]; then
    sed -i 's/127\.0\.0\.1/fastdfs-tracker/g' /app/conf/client.conf
    sed -i 's/192\.168\.52\.139/fastdfs-tracker/g' /app/conf/client.conf
    echo "FastDFS 配置已更新"
fi

# 修改后端配置（如果需要动态修改）
if [ -f /app/tc_http_server.conf ]; then
    # MySQL 地址替换
    sed -i 's/tuchuang_master_host=127\.0\.0\.1/tuchuang_master_host=mysql/g' /app/tc_http_server.conf
    sed -i 's/tuchuang_slave_host=127\.0\.0\.1/tuchuang_slave_host=mysql/g' /app/tc_http_server.conf

    # Redis 地址替换
    sed -i 's/token_host=127\.0\.0\.1/token_host=redis/g' /app/tc_http_server.conf
    sed -i 's/ranking_list_host=127\.0\.0\.1/ranking_list_host=redis/g' /app/tc_http_server.conf

    echo "后端配置已更新"
fi

# 打印配置内容以便调试
echo "=== FastDFS 配置 ==="
cat /app/conf/client.conf 2>/dev/null || echo "配置文件不存在"

echo "=== 后端配置 ==="
cat /app/tc_http_server.conf 2>/dev/null || echo "配置文件不存在"

# 查找并启动可执行文件
echo "查找可执行文件..."
BUILD_DIR="/build_output"
EXE=$(find "$BUILD_DIR" -name "tc_http_server" -type f -executable 2>/dev/null | head -1)

if [ -z "$EXE" ]; then
    echo "错误: 未找到 tc_http_server 可执行文件"
    ls -la "$BUILD_DIR" 2>/dev/null || echo "build_output 目录不存在"
    exit 1
fi

echo "找到可执行文件: $EXE"
echo "配置文件: ${1:-/app/tc_http_server.conf}"

# 启动应用
exec "$EXE" "${1:-/app/tc_http_server.conf}"