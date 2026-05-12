#!/bin/bash

# Docker 部署启动脚本

echo "=== 图床系统 Docker 部署 ==="

# 检查 Docker 和 Docker Compose
if ! command -v docker &> /dev/null; then
    echo "错误: Docker 未安装"
    exit 1
fi

if ! command -v docker compose &> /dev/null; then
    echo "错误: Docker Compose 未安装"
    exit 1
fi

# 检查 .env 文件
if [ ! -f .env ]; then
    echo "警告: .env 文件不存在，使用默认配置"
    cat > .env << 'EOF'
# 默认配置
MYSQL_ROOT_PASSWORD=123456
MYSQL_DATABASE=cloud_disk_db
MYSQL_USER=Boyu
MYSQL_PASSWORD=123456
MYSQL_PORT=3306
REDIS_PORT=6379
ENABLE_AI=false
DASHSCOPE_API_KEY=
EOF
    echo "已创建默认 .env 文件"
fi

# 加载 .env 并验证必要变量
set -a
source .env
set +a

# 验证必要变量
MISSING_VARS=""
[ -z "$MYSQL_ROOT_PASSWORD" ] && MISSING_VARS="$MISSING_VARS MYSQL_ROOT_PASSWORD"
[ -z "$MYSQL_DATABASE" ] && MISSING_VARS="$MISSING_VARS MYSQL_DATABASE"
[ -z "$MYSQL_USER" ] && MISSING_VARS="$MISSING_VARS MYSQL_USER"
[ -z "$MYSQL_PASSWORD" ] && MISSING_VARS="$MISSING_VARS MYSQL_PASSWORD"

if [ -n "$MISSING_VARS" ]; then
    echo "错误: 缺少必要的环境变量:$MISSING_VARS"
    exit 1
fi

echo "配置验证通过"

# 检查 AI 功能是否启用
if [ "$ENABLE_AI" = "true" ] && [ -z "$DASHSCOPE_API_KEY" ]; then
    echo "警告: ENABLE_AI=true 但未设置 DASHSCOPE_API_KEY，AI 功能将不可用"
fi

# 创建必要的目录
echo "创建必要的目录..."
mkdir -p ./data/mysql
mkdir -p ./data/redis
mkdir -p ./data/fastdfs/tracker
mkdir -p ./data/fastdfs/storage
mkdir -p ./nginx_tmp
mkdir -p ./data/chunks
mkdir -p ./data/frontend

# 检查前端构建目录是否存在
if [ ! -d "./AI_YunCunChu-main/picture_bed" ]; then
    echo "警告: 前端源码目录不存在，跳过前端构建"
    SKIP_FRONTEND=1
fi

# 构建并启动服务
echo "正在构建和启动服务..."
if [ "$SKIP_FRONTEND" = "1" ]; then
    docker compose up -d --build --ignore-frontend 2>/dev/null || docker compose up -d --build
else
    docker compose up -d --build
fi

# 等待服务启动
echo "等待服务启动..."
sleep 15

# 检查服务状态
echo "检查服务状态..."
docker compose ps

# 检查关键服务
echo ""
echo "=== 服务状态检查 ==="
docker compose ps | grep -E "(tc_nginx|tc_backend|tc_mysql|tc_redis|tc_fastdfs)" || true

echo ""
echo "=== 部署完成 ==="
echo "访问地址: http://localhost"
echo ""
echo "常用命令:"
echo "  查看日志: docker compose logs -f"
echo "  停止服务: docker compose down"
echo "  重启服务: docker compose restart"
echo "  进入容器: docker compose exec backend bash"