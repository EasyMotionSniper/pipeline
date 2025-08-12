# Docker 部署配置

以下是用于部署 Go 后端、任务执行器、MySQL 和 Redis 的 Docker 配置，满足您的需求：只暴露后端服务端口，其他服务隐藏在内部网络中。

## Dockerfile

首先创建一个多阶段构建的 Dockerfile，用于构建 Go 后端和任务执行器：


    


## Docker Compose 配置

创建 `docker-compose.yml` 文件，定义所有服务：


    


## 配置说明

### 服务组成

1. **MySQL**: 数据库服务，用于存储应用数据
   - 使用命名卷 `mysql-data` 持久化数据
   - 不暴露端口到宿主机，仅在内部网络可访问
   - 配置健康检查确保服务就绪

2. **Redis**: 用于 Asynq 的消息队列
   - 使用命名卷 `redis-data` 持久化数据
   - 不暴露端口到宿主机，仅在内部网络可访问

3. **Backend**: Go 后端服务
   - 唯一暴露到宿主机的服务，端口 `8080`
   - 依赖 MySQL 和 Redis 服务，确保它们就绪后才启动
   - 通过环境变量配置数据库和 Redis 连接

4. **Task Executor**: 任务执行器
   - 与后端通过 Redis (Asynq) 通信
   - 映射宿主机的 Docker socket，允许操作宿主机的 Docker
   - 不暴露端口到宿主机

### 网络配置

- 所有服务在同一网络 `app-network` 中，可通过服务名互相访问
- 只有后端服务的 `8080` 端口暴露到宿主机，其他服务完全隐藏

### 数据持久化

- MySQL 和 Redis 数据分别存储在命名卷中，确保容器重启后数据不丢失

## 启动和使用

1. 启动所有服务：
   ```bash
   docker-compose up -d
   ```

2. 查看服务状态：
   ```bash
   docker-compose ps
   ```

3. 查看日志：
   ```bash
   # 查看后端日志
   docker-compose logs -f backend
   
   # 查看任务执行器日志
   docker-compose logs -f task-executor
   ```

4. 停止服务：
   ```bash
   docker-compose down
   ```

5. 停止服务并清除数据卷：
   ```bash
   docker-compose down -v
   ```

这种配置确保了生产环境的安全性和隔离性，同时提供了开发和部署的便利性。



# 构建阶段
FROM golang:1.21-alpine AS builder

# 设置工作目录
WORKDIR /app

# 复制go模块文件
COPY go.mod go.sum ./
RUN go mod download

# 复制源代码
COPY . .

# 构建后端服务
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o backend ./cmd/backend

# 构建任务执行器
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o task-executor ./cmd/task-executor

# 运行阶段
FROM alpine:3.18

# 安装必要的工具
RUN apk --no-cache add ca-certificates tzdata

# 设置时区
ENV TZ=Asia/Shanghai

# 创建非root用户
RUN adduser -D -H -h /app appuser

# 设置工作目录
WORKDIR /app

# 从构建阶段复制二进制文件
COPY --from=builder /app/backend .
COPY --from=builder /app/task-executor .

# 复制配置文件（如果有）
COPY configs/ ./configs/

# 更改文件所有权
RUN chown -R appuser:appuser /app

# 切换到非root用户
USER appuser

# 暴露后端服务端口
EXPOSE 8080

# 提供默认命令（可以被docker-compose覆盖）
CMD ["./backend"]
    


version: '3.8'

services:
  # MySQL数据库
  mysql:
    image: mysql:8.0
    container_name: app-mysql
    restart: always
    environment:
      MYSQL_ROOT_PASSWORD: rootpassword
      MYSQL_DATABASE: appdb
      MYSQL_USER: appuser
      MYSQL_PASSWORD: apppassword
    volumes:
      - mysql-data:/var/lib/mysql
      - ./scripts/init.sql:/docker-entrypoint-initdb.d/init.sql # 初始化脚本（可选）
    networks:
      - app-network
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost", "-uappuser", "-papppassword"]
      interval: 10s
      timeout: 5s
      retries: 5
    # 不暴露端口到宿主机

  # Redis用于asynq
  redis:
    image: redis:7.2-alpine
    container_name: app-redis
    restart: always
    volumes:
      - redis-data:/data
    networks:
      - app-network
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5
    # 不暴露端口到宿主机

  # Go后端服务
  backend:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: app-backend
    restart: always
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    environment:
      - APP_ENV=production
      - DB_HOST=mysql
      - DB_PORT=3306
      - DB_USER=appuser
      - DB_PASSWORD=apppassword
      - DB_NAME=appdb
      - REDIS_ADDR=redis:6379
      - REDIS_PASSWORD=
      - PORT=8080
    networks:
      - app-network
    command: ["./backend"]
    # 只暴露后端服务端口到宿主机
    ports:
      - "8080:8080"

  # 任务执行器
  task-executor:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: app-task-executor
    restart: always
    depends_on:
      backend:
        condition: service_started
      redis:
        condition: service_healthy
    environment:
      - APP_ENV=production
      - DB_HOST=mysql
      - DB_PORT=3306
      - DB_USER=appuser
      - DB_PASSWORD=apppassword
      - DB_NAME=appdb
      - REDIS_ADDR=redis:6379
      - REDIS_PASSWORD=
    networks:
      - app-network
    # 映射宿主机的Docker socket，允许任务执行器操作宿主机Docker
    volumes:
      - /var/run/docker.sock:/var/run/docker.sock
    command: ["./task-executor"]
    # 不暴露端口到宿主机

networks:
  app-network:
    driver: bridge

volumes:
  mysql-data:
  redis-data:



以下是根据您的要求设计的完整解决方案，包含 Dockerfile 和 Docker Compose 配置，实现 Go 后端服务与异步任务执行器的容器化部署，同时集成 MySQL 和 Redis 服务，并确保网络隔离性。

🐳 Dockerfile 配置

1. Go 后端服务 Dockerfile (Dockerfile.backend)

# 构建阶段
FROM golang:1.21 AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . .
# 编译二进制（禁用CGO减小体积）
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o backend ./cmd/api

# 运行阶段（轻量级镜像）
FROM alpine:latest
WORKDIR /root/
COPY --from=builder /app/backend .
# 暴露HTTP端口（在Compose中映射）
EXPOSE 8080
CMD ["./backend"]


2. 任务执行器 Dockerfile (Dockerfile.worker)

# 复用构建阶段的二进制
FROM golang:1.21 AS builder
WORKDIR /app
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o worker ./cmd/worker

# 运行阶段
FROM alpine:latest
WORKDIR /root/
COPY --from=builder /app/worker .
# 不暴露端口（内部服务）
CMD ["./worker"]


🐙 Docker Compose 配置 (docker-compose.yml)

version: '3.8'

services:
  # Go 主后端服务（对外暴露）
  backend:
    build:
      context: .
      dockerfile: Dockerfile.backend
    ports:
      - "8080:8080"  # 暴露HTTP端口
    environment:
      - REDIS_ADDR=redis:6379
      - MYSQL_DSN=user:password@tcp(mysql:3306)/dbname
      - ASYNC_QUEUE=asynq  # Asynq队列名
    depends_on:
      - redis
      - mysql
    networks:
      - app-network

  # 异步任务执行器（不暴露端口）
  worker:
    build:
      context: .
      dockerfile: Dockerfile.worker
    environment:
      - REDIS_ADDR=redis:6379
      - ASYNC_QUEUE=asynq
    depends_on:
      - redis
    networks:
      - app-network

  # Redis 服务（仅容器内访问）
  redis:
    image: redis:7.0-alpine
    command: redis-server --requirepass your_redis_password  # 启用密码
    volumes:
      - redis-data:/data
    networks:
      - app-network

  # MySQL 服务（仅容器内访问）
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: rootpassword
      MYSQL_DATABASE: dbname
      MYSQL_USER: user
      MYSQL_PASSWORD: password
    volumes:
      - mysql-data:/var/lib/mysql
    networks:
      - app-network

# 网络与存储卷配置
volumes:
  redis-data:
  mysql-data:

networks:
  app-network:
    driver: bridge


🔒 关键安全设计

1. 网络隔离性
   • 所有服务通过 app-network 内部网络通信，仅 backend 服务暴露 8080 端口

   • MySQL/Redis 无端口映射，外部无法直接访问

2. 敏感信息保护
   • Redis 启用密码认证（--requirepass）

   • MySQL 通过环境变量配置独立用户（非 root 账户）

3. 异步通信机制
   • 后端通过 Asynq 提交任务到 Redis 队列

   • Worker 监听同一队列处理任务，解耦服务

4. 数据持久化
   • Redis 数据卷（redis-data）防止重启丢失

   • MySQL 数据卷（mysql-data）确保数据安全

⚙️ 部署与验证

1. 启动所有服务
docker compose up --build -d


2. 验证服务状态
docker compose ps

输出应仅显示 backend 的端口映射：
NAME                PORTS              
project-backend-1   0.0.0.0:8080->8080/tcp


3. 测试后端接口
curl http://localhost:8080/ping
# 预期返回: {"status":"ok"}


4. 查看异步任务
# 进入Redis容器查看Asynq队列
docker compose exec redis redis-cli -a your_redis_password
> KEYS asynq:*


📌 设计优势

1. 最小化攻击面  
   仅暴露必要的后端端口（8080），MySQL/Redis/Worker 完全隐藏

2. 资源隔离  
   • 每个服务独立容器，故障互不影响

   • 异步任务队列削峰填谷

3. 一键部署  
   开发/生产环境一致性，避免依赖冲突

4. 扩展性  
   • 横向扩展 Worker：docker compose up --scale worker=3 -d

   • 添加 Prometheus 监控（参考 ）

🔧 调试建议

1. 查看日志
# 后端日志
docker compose logs backend

# 异步任务日志
docker compose logs worker


2. 进入容器排查
# 进入Redis容器
docker compose exec redis sh

# 进入MySQL容器
docker compose exec mysql mysql -uuser -p


通过此方案，您获得了安全的微服务架构：前端仅与后端交互，异步任务和数据库完全封装在私有网络，同时保持部署的简洁性。



---

### **Dockerfile**

```Dockerfile
# 使用多阶段构建减少最终镜像体积
FROM golang:1.21 AS builder

# 构建阶段
WORKDIR /app
COPY . .

# 编译后端服务
RUN CGO_ENABLED=0 GOOS=linux go build -o /go/bin/backend -ldflags "-s -w" ./cmd/backend

# 编译 Task Executor
RUN CGO_ENABLED=0 GOOS=linux go build -o /go/bin/task-executor -ldflags "-s -w" ./cmd/executor

# 最终镜像
FROM gcr.io/distroless/static-debian13
WORKDIR /app

# 从 builder 阶段复制编译好的二进制文件
COPY --from=builder /go/bin/backend /app/backend
COPY --from=builder /go/bin/task-executor /app/task-executor

# 默认运行后端服务，可通过 docker-compose command 覆盖
CMD ["/app/backend"]
```

---

### **docker-compose.yml**

```yaml
version: '3.8'

services:
  # 后端服务
  backend:
    build: .
    ports:
      - "8080:8080"  # 暴露后端服务端口
    environment:
      - MYSQL_HOST=mysql
      - MYSQL_USER=root
      - MYSQL_PASSWORD=secret
      - MYSQL_DATABASE=mydb
      - REDIS_HOST=redis
      - REDIS_PORT=6379
      - ASYNQ_REDIS_CONN=redis://redis:6379
    depends_on:
      - mysql
      - redis

  # Task Executor 服务
  task-executor:
    build: .
    command: /app/task-executor
    environment:
      - ASYNQ_REDIS_CONN=redis://redis:6379
    depends_on:
      - redis

  # MySQL 服务
  mysql:
    image: mysql:8.0
    environment:
      - MYSQL_ROOT_PASSWORD=secret
      - MYSQL_DATABASE=mydb
      - MYSQL_USER=myuser
      - MYSQL_PASSWORD=mypassword
    volumes:
      - mysql_data:/var/lib/mysql
    ports:
      - "3306:3306"  # 本地调试时暴露，生产环境可移除

  # Redis 服务
  redis:
    image: redis:alpine
    ports:
      - "6379:6379"  # 本地调试时暴露，生产环境可移除

volumes:
  mysql_data:
```

---

### **目录结构说明**

```
your-project/
├── Dockerfile
├── docker-compose.yml
├── cmd/
│   ├── backend/      # 后端服务入口
│   └── executor/     # Task Executor 入口
├── internal/
│   ├── backend/      # HTTP 服务逻辑
│   └── executor/     # Task Executor 逻辑
├── go.mod
└── go.sum
```

---

### **关键配置说明**

| 服务 | 配置说明 |
|------|----------|
| **后端服务** | 
  - 暴露 `8080` 端口供外部访问。
  - 通过环境变量连接 `mysql` 和 `redis`。
  - 使用 `ASYNQ_REDIS_CONN` 指定 Asynq 的 Redis 地址。
| **Task Executor** |
  - 在 `docker-compose.yml` 中通过 `command` 覆盖默认命令，运行 `task-executor`。
  - 仅依赖 `redis`。
| **MySQL** |
  - 使用 `mysql:8.0` 镜像，配置默认用户名、密码和数据库。
  - 数据持久化到 `mysql_data` 卷。
| **Redis** |
  - 使用轻量级 `redis:alpine` 镜像。
  - 本地开发时暴露端口，生产环境建议移除 `ports` 配置。

---

### **启动流程**

1. **构建并启动服务**：
   ```bash
   docker-compose up --build
   ```

2. **访问后端服务**：
   ```
   http://localhost:8080
   ```

3. **验证 Task Executor 是否运行**：
   查看日志确认 Task Executor 已连接到 Redis 并监听队列：
   ```bash
   docker-compose logs task-executor
   ```

---

### **生产环境建议**

1. **隐藏敏感端口**：
   - 移除 `mysql` 和 `redis` 的 `ports` 配置，避免暴露到主机。
   - 仅保留 `backend` 的 `ports` 配置。

2. **使用 HTTPS**：
   在后端服务中启用 HTTPS，参考前文的 Gin HTTPS 配置。

3. **自动更新证书**：
   如果使用 Let's Encrypt 证书，可结合 `certbot` 自动续订。

4. **安全加固**：
   - MySQL 设置强密码。
   - Redis 配置密码认证（`requirepass`）。
   - 使用 `.env` 文件管理敏感信息，避免硬编码。

---

通过此配置，你可以高效运行 Go 后端服务和 Task Executor，同时隐藏内部依赖服务（MySQL、Redis），确保系统安全性与可维护性。



version: '3.8'

services:
  # MySQL数据库
  mysql:
    image: mysql:8.0
    container_name: app-mysql
    restart: always
    environment:
      MYSQL_ROOT_PASSWORD: rootpassword
      MYSQL_DATABASE: appdb
      MYSQL_USER: appuser
      MYSQL_PASSWORD: apppassword
    volumes:
      - mysql-data:/var/lib/mysql
      - ./scripts/init.sql:/docker-entrypoint-initdb.d/init.sql
    networks:
      - app-network
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost", "-uappuser", "-papppassword"]
      interval: 10s
      timeout: 5s
      retries: 5

  # Redis用于asynq
  redis:
    image: redis:7.2-alpine
    container_name: app-redis
    restart: always
    volumes:
      - redis-data:/data
    networks:
      - app-network
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  # Go后端服务（HTTPS）
  backend:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: app-backend
    restart: always
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    environment:
      - APP_ENV=production
      - DB_HOST=mysql
      - DB_PORT=3306
      - DB_USER=appuser
      - DB_PASSWORD=apppassword
      - DB_NAME=appdb
      - REDIS_ADDR=redis:6379
      - REDIS_PASSWORD=
      - HTTPS_PORT=443
      - TLS_CERT_PATH=/app/certs/fullchain.pem
      - TLS_KEY_PATH=/app/certs/privkey.pem
    networks:
      - app-network
    volumes:
      # 挂载SSL证书（宿主机上的证书目录）
      - ./certs:/app/certs:ro
    command: ["./backend"]
    # 暴露HTTPS端口
    ports:
      - "443:443"

  # 任务执行器
  task-executor:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: app-task-executor
    restart: always
    depends_on:
      backend:
        condition: service_started
      redis:
        condition: service_healthy
    environment:
      - APP_ENV=production
      - DB_HOST=mysql
      - DB_PORT=3306
      - DB_USER=appuser
      - DB_PASSWORD=apppassword
      - DB_NAME=appdb
      - REDIS_ADDR=redis:6379
      - REDIS_PASSWORD=
    networks:
      - app-network
    volumes:
      - /var/run/docker.sock:/var/run/docker.sock
    command: ["./task-executor"]

networks:
  app-network:
    driver: bridge

volumes:
  mysql-data:
  redis-data:
    


# 构建阶段
FROM golang:1.21-alpine AS builder

# 设置工作目录
WORKDIR /app

# 复制go模块文件
COPY go.mod go.sum ./
RUN go mod download

# 复制源代码
COPY . .

# 构建后端服务（支持HTTPS）
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o backend ./cmd/backend

# 构建任务执行器
RUN CGO_ENABLED=0 GOOS=linux go build -a -installsuffix cgo -o task-executor ./cmd/task-executor

# 运行阶段
FROM alpine:3.18

# 安装必要的工具
RUN apk --no-cache add ca-certificates tzdata

# 设置时区
ENV TZ=Asia/Shanghai

# 创建非root用户
RUN adduser -D -H -h /app appuser

# 创建证书目录
RUN mkdir -p /app/certs && chown -R appuser:appuser /app

# 设置工作目录
WORKDIR /app

# 从构建阶段复制二进制文件
COPY --from=builder /app/backend .
COPY --from=builder /app/task-executor .

# 复制配置文件
COPY configs/ ./configs/

# 切换到非root用户
USER appuser

# 暴露HTTPS端口
EXPOSE 443

# 提供默认命令
CMD ["./backend"]


在 Go 后端代码中，可以通过标准库 `os` 包读取环境变量，再将这些配置应用到数据库连接中。以下是具体实现方式，包含配置解析、默认值设置和数据库初始化的完整流程：


### **1. 配置结构体定义**
首先定义一个配置结构体，映射环境变量中的数据库配置项，方便管理和使用：

```go
// config/config.go
package config

import (
	"os"
	"strconv"
	"time"
)

// DBConfig 数据库配置结构体
type DBConfig struct {
	Host     string        // 数据库主机地址
	Port     int           // 数据库端口
	User     string        // 数据库用户名
	Password string        // 数据库密码
	Name     string        // 数据库名称
	Timeout  time.Duration // 连接超时时间
}

// RedisConfig Redis配置结构体（用于Asynq）
type RedisConfig struct {
	Addr     string // 地址（host:port）
	Password string // 密码
	DB       int    // 数据库编号
}

// TLSConfig HTTPS配置结构体
type TLSConfig struct {
	Port     string // HTTPS端口
	CertPath string // 证书路径
	KeyPath  string // 私钥路径
}

// Config 应用总配置
type Config struct {
	DB   DBConfig
	Redis RedisConfig
	TLS  TLSConfig
	Env  string // 环境（development/production）
}
```


### **2. 从环境变量加载配置**
编写配置加载函数，从环境变量中读取值，并设置默认值（防止环境变量未配置导致错误）：

```go
// config/loader.go
package config

import (
	"os"
	"strconv"
	"time"
)

// Load 从环境变量加载配置
func Load() *Config {
	return &Config{
		Env: getEnv("APP_ENV", "development"),
		DB: DBConfig{
			Host:     getEnv("DB_HOST", "localhost"),
			Port:     getEnvInt("DB_PORT", 3306), // MySQL默认端口
			User:     getEnv("DB_USER", "root"),
			Password: getEnv("DB_PASSWORD", ""),
			Name:     getEnv("DB_NAME", "appdb"),
			Timeout:  30 * time.Second, // 固定超时时间，也可从环境变量读取
		},
		Redis: RedisConfig{
			Addr:     getEnv("REDIS_ADDR", "localhost:6379"),
			Password: getEnv("REDIS_PASSWORD", ""),
			DB:       getEnvInt("REDIS_DB", 0),
		},
		TLS: TLSConfig{
			Port:     getEnv("HTTPS_PORT", "443"),
			CertPath: getEnv("TLS_CERT_PATH", "certs/fullchain.pem"),
			KeyPath:  getEnv("TLS_KEY_PATH", "certs/privkey.pem"),
		},
	}
}

// 从环境变量获取字符串，不存在则返回默认值
func getEnv(key, defaultValue string) string {
	if value, exists := os.LookupEnv(key); exists {
		return value
	}
	return defaultValue
}

// 从环境变量获取整数，不存在或解析失败则返回默认值
func getEnvInt(key string, defaultValue int) int {
	if value, exists := os.LookupEnv(key); exists {
		if intVal, err := strconv.Atoi(value); err == nil {
			return intVal
		}
	}
	return defaultValue
}
```


### **3. 初始化数据库连接**
使用加载的配置初始化 GORM 数据库连接（以 MySQL 为例）：

```go
// db/init.go
package db

import (
	"fmt"
	"yourproject/config"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

// 全局数据库实例
var DB *gorm.DB

// Init 初始化数据库连接
func Init(cfg *config.Config) error {
	// 构建MySQL连接DSN（Data Source Name）
	dsn := fmt.Sprintf(
		"%s:%s@tcp(%s:%d)/%s?charset=utf8mb4&parseTime=True&loc=Local",
		cfg.DB.User,
		cfg.DB.Password,
		cfg.DB.Host,
		cfg.DB.Port,
		cfg.DB.Name,
	)

	// 连接数据库
	db, err := gorm.Open(mysql.Open(dsn), &gorm.Config{})
	if err != nil {
		return fmt.Errorf("数据库连接失败: %v", err)
	}

	// 验证连接是否有效
	sqlDB, err := db.DB()
	if err != nil {
		return fmt.Errorf("获取底层DB失败: %v", err)
	}
	if err := sqlDB.Ping(); err != nil {
		return fmt.Errorf("数据库Ping失败: %v", err)
	}

	// 设置连接池参数
	sqlDB.SetMaxIdleConns(10)           // 最大空闲连接数
	sqlDB.SetMaxOpenConns(100)          // 最大打开连接数
	sqlDB.SetConnMaxLifetime(cfg.DB.Timeout) // 连接最大存活时间

	// 赋值给全局变量
	DB = db
	return nil
}
```


### **4. 在主程序中使用配置**
在程序入口（`main.go`）加载配置并初始化数据库：

```go
// cmd/backend/main.go
package main

import (
	"log"
	"yourproject/config"
	"yourproject/db"
	"yourproject/server" // 假设包含HTTP服务初始化逻辑
)

func main() {
	// 1. 加载配置
	cfg := config.Load()
	log.Printf("加载配置完成，环境: %s", cfg.Env)

	// 2. 初始化数据库
	if err := db.Init(cfg); err != nil {
		log.Fatalf("数据库初始化失败: %v", err)
	}
	log.Println("数据库连接成功")

	// 3. 初始化Asynq客户端（用于任务调度）
	asynqClient := initAsynqClient(cfg) // 实现见下方

	// 4. 启动HTTPS服务
	log.Printf("启动HTTPS服务，端口: %s", cfg.TLS.Port)
	if err := server.RunHTTPS(cfg, asynqClient); err != nil {
		log.Fatalf("服务启动失败: %v", err)
	}
}

// 初始化Asynq客户端（使用Redis配置）
func initAsynqClient(cfg *config.Config) *asynq.Client {
	client := asynq.NewClient(asynq.RedisClientOpt{
		Addr:     cfg.Redis.Addr,
		Password: cfg.Redis.Password,
		DB:       cfg.Redis.DB,
	})
	return client
}
```


### **5. HTTPS服务启动（结合TLS配置）**
在 HTTP 服务初始化中使用 TLS 配置：

```go
// server/server.go
package server

import (
	"yourproject/config"
	"yourproject/handler"

	"github.com/gin-gonic/gin"
	"github.com/hibiken/asynq"
)

// RunHTTPS 启动HTTPS服务
func RunHTTPS(cfg *config.Config, asynqClient *asynq.Client) error {
	r := gin.Default()

	// 注册路由（示例）
	r.GET("/health", handler.HealthCheck)

	// 使用配置的TLS证书启动HTTPS服务
	return r.RunTLS(":"+cfg.TLS.Port, cfg.TLS.CertPath, cfg.TLS.KeyPath)
}
```


### **核心逻辑说明**
1. **环境变量读取**：通过 `os.LookupEnv` 读取环境变量，封装 `getEnv` 和 `getEnvInt` 函数简化读取逻辑，并支持默认值。
2. **配置结构化**：使用 `Config` 结构体统一管理数据库、Redis、TLS 等配置，代码更清晰，便于维护。
3. **数据库连接**：根据配置动态构建 DSN，支持连接池参数设置，确保数据库连接的可靠性和性能。
4. **与Docker配合**：Docker Compose 中定义的环境变量（如 `DB_HOST=mysql`）会被容器内的 Go 程序读取，实现容器间的通信（如后端连接 `mysql` 服务）。


### **关键优势**
- **灵活性**：通过环境变量而非硬编码配置，可在不同环境（开发/测试/生产）中灵活切换配置，无需修改代码。
- **安全性**：敏感信息（如数据库密码）通过环境变量注入，避免暴露在代码或配置文件中。
- **可扩展性**：新增配置项时，只需扩展 `Config` 结构体和加载逻辑，无需大面积修改代码。

这种方式完全适配之前的 Docker 配置，确保后端服务能正确读取 Docker Compose 中定义的数据库、Redis 等环境变量。