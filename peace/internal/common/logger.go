package common

import (
	"os"
	"time"

	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
	"gopkg.in/natefinch/lumberjack.v2"
)

var logger *zap.Logger

func GetLogger() *zap.Logger {
	return logger
}

func InitLog() {
	logPath := os.Getenv("LOG_PATH")

	// 配置日志轮转
	writeSyncer := zapcore.AddSync(&lumberjack.Logger{
		Filename:   logPath, // 日志文件路径
		MaxSize:    10,      // 单个文件最大大小（MB）
		MaxBackups: 10,      // 保留最大备份数
		MaxAge:     7,       // 保留最大天数（天）
		LocalTime:  true,
	})

	// 自定义时间编码器：使用本地时间并格式化
	customTimeEncoder := func(t time.Time, enc zapcore.PrimitiveArrayEncoder) {
		// 格式化本地时间为 "2006-01-02 15:04:05.000"（年-月-日 时:分:秒.毫秒）
		localTime := t.Local()
		enc.AppendString(localTime.Format("2006-01-02 15:04:05.000"))
	}

	// 配置控制台格式编码器（非JSON）
	encoderConfig := zapcore.EncoderConfig{
		TimeKey:        "T", // 时间字段的键名
		LevelKey:       "L", // 级别字段的键名
		MessageKey:     "M", // 消息字段的键名
		CallerKey:      "C", // 调用者字段的键名
		NameKey:        "N", // 日志器名称字段的键名
		StacktraceKey:  "S", // 堆栈跟踪字段的键名
		LineEnding:     zapcore.DefaultLineEnding,
		EncodeLevel:    zapcore.CapitalLevelEncoder, // 级别大写（INFO/WARN/ERROR）
		EncodeTime:     customTimeEncoder,           // 使用自定义本地时间编码器
		EncodeDuration: zapcore.StringDurationEncoder,
		EncodeCaller:   zapcore.ShortCallerEncoder, // 调用者信息（短路径，如 pkg/file.go:123）
	}

	// 使用控制台编码器（非JSON格式）
	encoder := zapcore.NewConsoleEncoder(encoderConfig)

	// 配置日志级别（debug/info/warn/error）
	level := zapcore.InfoLevel

	// 创建zap logger，添加调用者信息
	core := zapcore.NewCore(encoder, writeSyncer, level)
	logger = zap.New(core, zap.AddCaller())
}
