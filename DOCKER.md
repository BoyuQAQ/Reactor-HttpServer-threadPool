# Docker 部署文档

## 快速开始

### 前置要求
- Docker Engine 20.10+
- Docker Compose 2.0+
- 至少 4GB 可用内存
- 至少 20GB 可用磁盘空间

### 部署步骤

1. **创建必要目录**
   ```bash
   mkdir -p data/mysql data/redis data/fastdfs/tracker data/fastdfs/storage nginx_tmp
   mkdir -p /tmp/chunks
   ```

2. **配置环境变量**
   编辑 `.env` 文件，根据需要修改配置：
   ```bash
   # MySQL 配置
   MYSQL_ROOT_PASSWORD=123456
   MYSQL_DATABASE=cloud_disk_db
   MYSQL_USER=Boyu
   MYSQL_PASSWORD=123456

   # Redis 配置
   REDIS_PORT=6379

   # AI 功能（可选）
   DASHSCOPE_API_KEY=your_api_key_here

   # 启动选项
   # 开发环境: WAIT_DEPENDENCIES=false（跳过等待，加快启动）
   # 生产环境: WAIT_DEPENDENCIES=true（确保依赖就绪）
   WAIT_DEPENDENCIES=true
   ```

3. **启动服务**
   ```bash
   # Linux/Mac
   chmod +x start.sh
   ./start.sh

   # Windows (PowerShell)
   docker compose up -d --build
   ```

4. **验证部署**
   - 访问 http://localhost 查看前端页面
   - API 测试: http://localhost/api/login

## 服务架构

```
┌─────────────────────────────────────────────────────────────┐
│                        Nginx (Port 80)                      │
│         静态文件 + API 代理 + FastDFS 文件服务              │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┴─────────────────────┐
        │                                             │
        ▼                                             ▼
┌───────────────────┐                     ┌───────────────────┐
│  C++ Backend      │                     │   FastDFS         │
│  (Port 8081)     │                     │   Tracker +       │
│  Reactor 架构     │                     │   Storage         │
└───────────────────┘                     └───────────────────┘
        │                                             ▲
        │                                             │
        │                                             │
   ┌────┴────┐                                   │
   │         │                                   │
   ▼         ▼                                   │
┌──────┐ ┌──────┐                                │
│ MySQL│ │Redis │◄────────────────────────────────┘
│3306  │ │6379  │
└──────┘ └──────┘
```

## 服务说明

| 服务       | 容器名        | 端口映射          | 说明              |
|------------|---------------|-------------------|-------------------|
| MySQL      | tc_mysql      | 3306:3306         | 数据库            |
| Redis      | tc_redis      | 6379:6379         | 缓存/会话        |
| FastDFS    | tc_fastdfs_*  | 22122, 23000      | 文件存储         |
| Nginx      | tc_nginx      | 80:80, 443:443   | 反向代理         |
| Backend    | tc_backend    | 8081:8081         | C++ API 服务     |
| Frontend   | tc_frontend   | (通过 Nginx 访问) | React 管理界面   |

## 常用命令

```bash
# 查看所有容器状态
docker compose ps

# 查看日志
docker compose logs -f [服务名]

# 重启单个服务
docker compose restart backend

# 停止所有服务
docker compose down

# 重新构建
docker compose up -d --build

# 进入容器调试
docker compose exec backend bash
docker compose exec mysql sh
```

## 目录结构

```
picture_bed/
├── .env                    # 环境变量配置
├── docker-compose.yml      # 服务编排
├── nginx.conf              # Nginx 配置
├── Dockerfile.backend      # C++ 后端构建
├── Dockerfile.frontend     # React 前端构建
├── start.sh                # 启动脚本
├── tuchuang.sql            # 数据库初始化脚本
├── index.html              # 前端页面
├── tc_http_server.conf     # 后端配置文件
├── frontend/
│   └── nginx.conf          # 前端 Nginx 配置
├── AI_YunCunChu-main/      # AI 功能源码
└── data/                   # 数据目录（自动创建）
    ├── mysql/
    ├── redis/
    └── fastdfs/
```

## 注意事项

1. 首次启动需要编译 C++ 后端，耗时约 5-10 分钟
2. FastDFS 首次启动需要初始化存储目录
3. MySQL 初始化会自动导入 tuchuang.sql
4. 生产环境请修改默认密码
5. 如需启用 AI 功能，请设置 DASHSCOPE_API_KEY

## 故障排查

### 服务无法启动
```bash
# 检查端口占用
netstat -tlnp | grep -E '80|3306|6379|8081'

# 检查容器日志
docker compose logs backend
```

### 数据库连接失败
```bash
# 检查 MySQL 容器
docker compose exec mysql mysql -u root -p

# 查看 MySQL 日志
docker compose logs mysql
```

### FastDFS 文件上传失败
```bash
# 检查 FastDFS 状态
docker compose exec fastdfs-storage fdfs_monitor /etc/fdfs/client.conf

# 检查存储目录
docker compose exec fastdfs-storage ls /fastdfs/storage/data
```

## 数据备份

### 自动备份（每天凌晨 3 点）
```bash
# 安装定时任务
crontab crontab.conf

# 或手动执行备份
chmod +x scripts/backup.sh
./scripts/backup.sh
```

### 手动恢复
```bash
chmod +x scripts/restore.sh
./scripts/restore.sh ./backups/cloud_disk_db_20240512_143000.sql.gz
```

### 备份文件位置
- 目录: `./backups/`
- 命名: `cloud_disk_db_YYYYMMDD_HHMMSS.sql.gz`
- 默认保留 7 天

## 监控

### 运行监控脚本
```bash
chmod +x scripts/monitor.sh
./scripts/monitor.sh
```

### 监控内容
- 容器健康状态
- CPU/内存使用率
- 磁盘使用情况
- 最近 5 分钟错误日志

### 查看实时日志
```bash
# 所有服务
docker compose logs -f

# 指定服务
docker compose logs -f backend
docker compose logs -f mysql

# 最近 100 行
docker compose logs --tail=100 backend
```

## 资源限制

| 服务 | CPU 限制 | 内存限制 |
|------|----------|----------|
| MySQL | - | 512MB |
| Redis | - | 256MB |
| Backend | 1核 | 512MB |
| Nginx | - | - |

## Redis 持久化

已开启以下持久化策略：
- **AOF**: 每秒同步一次
- **RDB**: 900秒(1次)、300秒(10次)、60秒(10000次)
- **内存策略**: allkeys-lru（内存满时淘汰最少使用的键）

容器重启后数据不会丢失。