#!/bin/bash
# wait-for.sh - 等待依赖服务就绪后启动应用

set -e

echo "========================================="
echo "等待依赖服务就绪..."
echo "========================================="

# 等待函数
wait_for_service() {
    local host=$1
    local port=$2
    local name=$3
    local max_attempts=${4:-30}
    local interval=${5:-2}

    echo "等待 $name ($host:$port)..."

    for i in $(seq 1 $max_attempts); do
        if nc -z -w 1 $host $port 2>/dev/null; then
            echo "$name 已就绪 (尝试 $i/$max_attempts)"
            return 0
        fi
        echo "  尝试 $i/$max_attempts: $name 未就绪，等待 ${interval}s..."
        sleep $interval
    done

    echo "错误: $name 在 ${max_attempts} 次尝试后仍未就绪"
    return 1
}

# 等待 MySQL (最多 60 秒)
if ! wait_for_service "mysql" 3306 "MySQL" 30 2; then
    echo "错误: MySQL 启动失败，退出"
    exit 1
fi

# 等待 Redis (最多 30 秒)
if ! wait_for_service "redis" 6379 "Redis" 15 2; then
    echo "错误: Redis 启动失败，退出"
    exit 1
fi

# 等待 FastDFS Tracker (最多 30 秒)
if ! wait_for_service "fastdfs-tracker" 22122 "FastDFS Tracker" 15 2; then
    echo "错误: FastDFS Tracker 启动失败，退出"
    exit 1
fi

# 等待 FastDFS Storage (最多 60 秒)
echo "等待 FastDFS Storage 就绪（检查连接状态）..."
for i in $(seq 1 30); do
    if nc -z -w 1 fastdfs-storage 23000 2>/dev/null; then
        # Storage 端口可达，验证状态
        echo "  Storage 端口可达，检查 Active 状态..."
        sleep 2
        if fdfs_monitor /etc/fdfs/client.conf 2>/dev/null | grep -q "ACTIVE"; then
            echo "FastDFS Storage 已就绪 (尝试 $i/30)"
            break
        fi
    fi
    echo "  尝试 $i/30: Storage 未就绪，等待 2s..."
    sleep 2

    if [ $i -eq 30 ]; then
        echo "警告: FastDFS Storage 可能未完全就绪，继续尝试启动..."
    fi
done

echo "========================================="
echo "所有依赖服务已就绪，启动应用..."
echo "========================================="

exec "$@"