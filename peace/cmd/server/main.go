package main

import (
	"pace/internal/common"
	"pace/internal/server/handler"

	"github.com/gin-gonic/gin"
)

func main() {
	config := common.GetConfig()
	logger := common.GetLogger()
	defer logger.Sync()

	// gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.POST("/login", handler.UserLogin)
	logger.Info("hello from server")
	// r.Use(middleware.JWTAuthMiddleware())

	r.POST("/update/:id", handler.UpdatePipeline)
	r.POST("/create", handler.CreatePipeline)
	r.POST("/trigger", handler.TriggerPipeline)
	r.GET("/pipeline", handler.ListPipelines)
	r.GET("/pipeline/:id", handler.ListPipelineDetail)
	r.GET("/history", handler.ListExecutionHistory)
	r.GET("/history/:id", handler.ListExecutionHistoryDetail)
	r.POST("/webhook", handler.Webhook)

	err := r.RunTLS(":8080", config.CertPath, config.KeyPath)
	if err != nil {
		panic(err)
	}

}
