#!/bin/bash
# MySQL 数据库恢复脚本
# 用法: ./restore.sh <backup_file>

set -e

if [ -z "$1" ]; then
    echo "用法: $0 <backup_file>"
    echo "示例: $0 ./backups/cloud_disk_db_20240512_143000.sql.gz"
    exit 1
fi

BACKUP_FILE="$1"

if [ ! -f "$BACKUP_FILE" ]; then
    echo "错误: 备份文件不存在: $BACKUP_FILE"
    exit 1
fi

# 配置
MYSQL_HOST="${MYSQL_HOST:-mysql}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
MYSQL_DATABASE="${MYSQL_DATABASE:-cloud_disk_db}"

echo "=== 开始恢复数据库: $MYSQL_DATABASE ==="
echo "备份文件: $BACKUP_FILE"

# 确认恢复
read -p "确认恢复? (输入 'yes' 继续): " confirm
if [ "$confirm" != "yes" ]; then
    echo "取消恢复"
    exit 0
fi

# 执行恢复
gunzip -c "$BACKUP_FILE" | docker compose exec -T mysql mysql \
    -h"$MYSQL_HOST" \
    -P"$MYSQL_PORT" \
    -u"$MYSQL_USER" \
    -p"$MYSQL_PASSWORD" \
    "$MYSQL_DATABASE"

echo ""
echo "=== 恢复完成 ==="