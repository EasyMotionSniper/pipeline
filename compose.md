version: '3.8'

services:
  # 镜像预加载服务
  image-preloader:
    image: docker.1ms.run/alpine:3.17
    container_name: image-preloader
    command: echo "Alpine image preloaded"
    restart: "no"  # 只运行一次
    networks:
      - app-network

  # MySQL数据库
  mysql:
    # ... 保持原有配置

  redis:
    # ... 保持原有配置

  backend:
    # ... 保持原有配置

  # 任务执行器
  task-executor:
    build: .
    container_name: go-task-executor
    restart: always
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
      backend:
        condition: service_started
      image-preloader:
        condition: service_completed_successfully  # 等待镜像加载完成
    environment:
      - APP_ENV=production
      - REDIS_ADDR=redis:6379
      - LOG_PATH=/app/logs/executor.log
      - ALPINE_IMAGE=alpine:3.17  # 可以通过环境变量传递镜像名
    volumes:
      - ./logs/executor:/app/logs
      - /var/run/docker.sock:/var/run/docker.sock
      - /etc/localtime:/etc/localtime:ro
      - /etc/timezone:/etc/timezone:ro
    networks:
      - app-network
    expose: []
    command: ["task_executor"]

# ... 其余配置保持不变