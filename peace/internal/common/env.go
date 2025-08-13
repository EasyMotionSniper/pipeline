package common

import (
	"os"
	"strconv"
)

// Config 应用配置结构体，对应docker-compose中的环境变量
type Config struct {
	AppEnv     string // 环境（如production）
	DBHost     string // 数据库主机
	DBPort     int    // 数据库端口
	DBUser     string // 数据库用户名
	DBPassword string // 数据库密码
	DBName     string // 数据库名
	RedisAddr  string // Redis地址（格式：host:port）
	LogPath    string // 日志文件路径
	KeyPath    string // 密钥文件路径
	CertPath   string // 证书文件路径
}

var config Config

func GetConfig() Config {
	return config
}

func InitConf() {
	dbPort, _ := strconv.Atoi(getEnv("DB_PORT", "3306"))

	config = Config{
		AppEnv:     getEnv("APP_ENV", "development"), // 默认开发环境
		DBHost:     getEnv("DB_HOST", "localhost"),   // 默认本地主机（容器内实际用mysql服务名）
		DBPort:     dbPort,
		DBUser:     getEnv("DB_USER", ""),                  // 无默认值，必须在环境变量中配置
		DBPassword: getEnv("DB_PASSWORD", ""),              // 无默认值，必须配置
		DBName:     getEnv("DB_NAME", "app_db"),            // 默认数据库名
		RedisAddr:  getEnv("REDIS_ADDR", "localhost:6379"), // 默认Redis地址
		LogPath:    getEnv("LOG_PATH", "./logs/app.log"),   // 默认日志路径
		KeyPath:    getEnv("KEY_PATH", ""),
		CertPath:   getEnv("CERT_PATH", ""),
	}
}

func getEnv(key, defaultValue string) string {
	value := os.Getenv(key)
	if value == "" {
		return defaultValue
	}
	return value
}
