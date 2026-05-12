# 自定义 Nginx 镜像（包含 FastDFS 模块）
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装 nginx 和 FastDFS 模块
RUN apt-get update && apt-get install -y \
    nginx \
    libnginx-mod-http-fastdfs-module \
    && rm -rf /var/lib/apt/lists/*

# 复制 nginx 配置
COPY nginx.conf /etc/nginx/nginx.conf

# 创建前端静态文件目录
RUN mkdir -p /usr/share/nginx/html

# 暴露端口
EXPOSE 80 443

CMD ["nginx", "-g", "daemon off;"]