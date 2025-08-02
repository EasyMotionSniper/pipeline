package main

import (
	"net/http"
	"pace/internal/handler"
	"pace/internal/model"

	"github.com/gin-gonic/gin"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

func main() {

	dsn := "root:13616749175ymq@tcp(localhost:3306)/pace?charset=utf8mb4&parseTime=True&loc=Local"
	db, err := gorm.Open(mysql.Open(dsn))
	if err != nil {
		panic("failed to connect database")
	}

	// 自动迁移模型
	db.AutoMigrate(&model.User{}, &model.Pipeline{}, &model.PipelineExecution{}, &model.TaskExecution{})

	// gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.GET("/login", handler.UserLogin)

	r.GET("/pipeline", func(c *gin.Context) {
		var pipelines []model.Pipeline
		db.Find(&pipelines)
		c.JSON(http.StatusOK, gin.H{
			"pipelines": pipelines,
		})
	})
	r.GET("/pipeline/:id", func(c *gin.Context) {
		id := c.Param("id")
		var pipeline model.Pipeline
		// first ?
		db.First(&pipeline, id)
		c.JSON(http.StatusOK, gin.H{
			"pipeline": pipeline,
		})
	})
	r.Run(":8080")
}
