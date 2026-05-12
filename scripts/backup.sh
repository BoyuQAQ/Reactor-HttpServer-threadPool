#!/bin/bash
# MySQL 数据库备份脚本
# 用法: ./backup.sh [keep_days]
# keep_days: 保留天数，默认 7 天

set -e

# 配置
BACKUP_DIR="${BACKUP_DIR:-./backups}"
MYSQL_HOST="${MYSQL_HOST:-mysql}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
MYSQL_DATABASE="${MYSQL_DATABASE:-cloud_disk_db}"
KEEP_DAYS="${1:-7}"

# 创建备份目录
mkdir -p "$BACKUP_DIR"

# 生成备份文件名
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_FILE="${BACKUP_DIR}/${MYSQL_DATABASE}_${TIMESTAMP}.sql.gz"

echo "=== 开始备份数据库: $MYSQL_DATABASE ==="

# 执行备份
docker compose exec -T mysql mysqldump \
    -h"$MYSQL_HOST" \
    -P"$MYSQL_PORT" \
    -u"$MYSQL_USER" \
    -p"$MYSQL_PASSWORD" \
    --single-transaction \
    --quick \
    --lock-tables=false \
    "$MYSQL_DATABASE" | gzip > "$BACKUP_FILE"

# 检查备份结果
if [ -f "$BACKUP_FILE" ] && [ -s "$BACKUP_FILE" ]; then
    FILE_SIZE=$(du -h "$BACKUP_FILE" | cut -f1)
    echo "备份成功: $BACKUP_FILE (大小: $FILE_SIZE)"
else
    echo "备份失败!"
    exit 1
fi

# 清理过期备份
echo "清理超过 $KEEP_DAYS 天的备份..."
find "$BACKUP_DIR" -name "*.sql.gz" -type f -mtime +$KEEP_DAYS -delete

# 显示保留的备份列表
echo ""
echo "=== 当前备份列表 ==="
ls -lh "$BACKUP_DIR"/*.sql.gz 2>/dev/null || echo "无备份文件"

echo ""
echo "=== 备份完成 ==="
echo "备份目录: $(realpath $BACKUP_DIR)"
echo "保留天数: $KEEP_DAYS 天"