#!/bin/bash
# 服务监控脚本
# 用法: ./monitor.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=== 图床服务监控 ==="
echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# 检查容器状态
check_container() {
    local name=$1
    local status=$(docker compose ps -q "$name" 2>/dev/null | xargs docker inspect --format='{{.State.Health.Status}}' 2>/dev/null || echo "unknown")

    if [ "$status" = "healthy" ]; then
        echo -e "${GREEN}[OK]${NC} $name"
        return 0
    elif [ "$status" = "starting" ]; then
        echo -e "${YELLOW}[启动中]${NC} $name"
        return 0
    else
        echo -e "${RED}[异常]${NC} $name (状态: $status)"
        return 1
    fi
}

FAILED=0

# 检查所有服务
services=("mysql" "redis" "fastdfs-tracker" "fastdfs-storage" "nginx" "backend")

for svc in "${services[@]}"; do
    if ! check_container "$svc"; then
        FAILED=$((FAILED + 1))
    fi
done

echo ""

# 检查资源使用
echo "=== 资源使用 ==="
docker stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}" 2>/dev/null || echo "无法获取资源统计"

echo ""

# 检查磁盘使用
echo "=== 磁盘使用 ==="
for vol in mysql_data redis_data fastdfs_tracker_data fastdfs_storage_data; do
    size=$(docker volume inspect tc_http_server_${vol} --format='{{.UsageData.Size}}' 2>/dev/null || echo "N/A")
    echo "$vol: $size"
done

echo ""

# 检查日志错误
echo "=== 最近错误日志 ==="
ERROR_COUNT=$(docker compose logs --since=5m 2>/dev/null | grep -i "error\|fatal\|exception" | wc -l || echo "0")
if [ "$ERROR_COUNT" -gt 0 ]; then
    echo -e "${RED}发现 $ERROR_COUNT 个错误${NC}"
    docker compose logs --since=5m 2>/dev/null | grep -i "error\|fatal\|exception" | tail -5
else
    echo -e "${GREEN}无错误${NC}"
fi

echo ""

# 总结
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}=== 所有服务正常 ===${NC}"
    exit 0
else
    echo -e "${RED}=== $FAILED 个服务异常 ===${NC}"
    exit 1
fi