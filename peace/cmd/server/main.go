package main

import (
	"net/http"
	"pace/internal/server/handler"
	"pace/internal/server/model"

	"github.com/gin-gonic/gin"
)

func main() {

	// gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.POST("/login", handler.UserLogin)

	// r.Use(middleware.JWTAuthMiddleware())

	r.POST("/pipeline/update/:name", handler.UpdatePipeline)
	r.POST("/pipeline/create", handler.CreatePipeline)
	r.POST("/trigger", handler.TriggerPipeline)
	r.GET("/pipeline", func(c *gin.Context) {
		var pipelines []model.Pipeline
		// db.Find(&pipelines)
		c.JSON(http.StatusOK, gin.H{
			"pipelines": pipelines,
		})
	})
	r.GET("/pipeline/:id", func(c *gin.Context) {
		// id := c.Param("id")
		var pipeline model.Pipeline
		// first ?
		// db.First(&pipeline, id)
		c.JSON(http.StatusOK, gin.H{
			"pipeline": pipeline,
		})
	})

	// go func() {
	// 	server := &rpccall.CallbackServer{}

	// 	server.Start(":8082")
	// }()
	r.Run(":8080")

}
