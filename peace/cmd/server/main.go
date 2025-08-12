package main

import (
	"pace/internal/server/handler"

	"github.com/gin-gonic/gin"
)

func main() {

	// gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.POST("/login", handler.UserLogin)

	// r.Use(middleware.JWTAuthMiddleware())

	r.POST("/update/:id", handler.UpdatePipeline)
	r.POST("/create", handler.CreatePipeline)
	r.POST("/trigger", handler.TriggerPipeline)
	r.GET("/pipeline", handler.ListPipelines)
	r.GET("/pipeline/:id", handler.ListPipelineDetail)
	r.GET("/history", handler.ListExecutionHistory)
	r.GET("/history/:id", handler.ListExecutionHistoryDetail)
	r.POST("/webhook", handler.Webhook)
	r.Run(":8080")

}
