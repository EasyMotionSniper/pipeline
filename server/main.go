package main

import (
	"fmt"
	"os"
)

func main() {
	// 检查Docker是否可用
	if !isDockerAvailable() {
		fmt.Println("Error: Docker is not available. Please install Docker and try again.")
		os.Exit(1)
	}

	// 1. 创建调度器
	scheduler := NewScheduler()

	// 2. 定义任务（示例：多个任务创建同名目录，但在独立Docker容器中）
	tasks := []*Task{
		{
			ID:           "t1",
			Name:         "Create test directory 1",
			Command:      "mkdir test && echo 't1: test directory created' && ls",
			Dependencies: []string{}, // 无依赖
		},
		{
			ID:           "t2",
			Name:         "Create test directory 2 (depends on t1)",
			Command:      "mkdir test && echo 't2: test directory created' && ls",
			Dependencies: []string{"t1"}, // 依赖t1完成
		},
		{
			ID:           "t3",
			Name:         "Create test directory 3 (depends on t2)",
			Command:      "mkdir test && echo 't3: test directory created' && ls",
			Dependencies: []string{"t2"}, // 依赖t2完成
		},
		{
			ID:           "t4",
			Name:         "Create test directory 4 (no dependencies)",
			Command:      "mkdir test && echo 't4: test directory created' && ls",
			Dependencies: []string{}, // 无依赖
		},
	}

	// 3. 添加任务到调度器
	for _, t := range tasks {
		if err := scheduler.AddTask(t); err != nil {
			fmt.Printf("Failed to add task: %v\n", err)
			return
		}
	}

	// 4. 执行所有任务
	if err := scheduler.Run(); err != nil {
		fmt.Printf("Scheduler failed: %v\n", err)
		return
	}

	fmt.Println("All tasks completed successfully")
	/*
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
	*/
}
