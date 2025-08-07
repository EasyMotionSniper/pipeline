package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"
	_ "github.com/go-sql-driver/mysql"
	"github.com/robfig/cron/v3"
)

// 全局变量
var (
	db             *sql.DB
	taskExecutor   *TaskExecutor
	cronScheduler  *cron.Cron
	taskStatusChan = make(chan TaskStatusUpdate, 100) // 任务状态更新通道
)

func main() {
	// 初始化数据库
	initDB()
	defer db.Close()

	// 初始化任务执行器
	taskExecutor = NewTaskExecutor(5) // 最大并发数5

	// 初始化Cron调度器
	cronScheduler = cron.New(cron.WithSeconds())
	defer cronScheduler.Stop()
	cronScheduler.Start()

	// 启动任务状态处理协程
	go processTaskStatusUpdates()

	// 初始化Gin
	r := gin.Default()

	// 路由设置
	r.POST("/pipelines", uploadPipeline)
	r.GET("/pipelines", listPipelines)
	r.GET("/pipelines/:id", getPipeline)
	r.POST("/pipelines/:id/trigger", triggerPipeline)
	r.GET("/tasks/:id", getTaskStatus)

	// 启动服务器
	srv := &http.Server{
		Addr:    ":8080",
		Handler: r,
	}

	go func() {
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("listen: %s\n", err)
		}
	}()

	// 等待中断信号优雅关闭服务器
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit
	log.Println("Shutting down server...")

	// 停止任务执行器
	taskExecutor.Stop()

	// 关闭服务器
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := srv.Shutdown(ctx); err != nil {
		log.Fatal("Server forced to shutdown:", err)
	}

	log.Println("Server exiting")
}

// 处理任务状态更新
func processTaskStatusUpdates() {
	for update := range taskStatusChan {
		log.Printf("Processing task update: %+v\n", update)
		err := updateTaskStatusInDB(update)
		if err != nil {
			log.Printf("Failed to update task status in DB: %v", err)
		}

		// 检查是否有依赖任务可以执行
		dependentTasks, err := getDependentTasks(update.TaskID)
		if err != nil {
			log.Printf("Failed to get dependent tasks: %v", err)
			continue
		}

		for _, task := range dependentTasks {
			allDependenciesCompleted, err := checkAllDependenciesCompleted(task.ID)
			if err != nil {
				log.Printf("Failed to check dependencies: %v", err)
				continue
			}

			if allDependenciesCompleted {
				log.Printf("All dependencies completed for task %s, submitting for execution", task.ID)
				taskExecutor.SubmitTask(task)
			}
		}

		// 检查流水线是否已完成
		checkPipelineCompletion(update.PipelineID)
	}
}

// 上传流水线定义
func uploadPipeline(c *gin.Context) {
	file, _, err := c.Request.FormFile("pipeline")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Failed to get file: " + err.Error()})
		return
	}
	defer file.Close()

	// 解析YAML
	pipeline, err := ParsePipelineYAML(file)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Failed to parse YAML: " + err.Error()})
		return
	}

	// 保存到数据库
	pipelineID, err := savePipelineToDB(pipeline)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save pipeline: " + err.Error()})
		return
	}

	// 如果有cron触发方式，添加到定时任务
	for _, trigger := range pipeline.Triggers {
		if trigger.Type == "cron" {
			_, err := cronScheduler.AddFunc(trigger.Expression, func() {
				log.Printf("Cron trigger for pipeline %s", pipelineID)
				triggerPipelineByID(pipelineID)
			})
			if err != nil {
				log.Printf("Failed to add cron job: %v", err)
				// 不中断，继续处理
			} else {
				log.Printf("Added cron job for pipeline %s: %s", pipelineID, trigger.Expression)
			}
		}
	}

	c.JSON(http.StatusOK, gin.H{
		"message":    "Pipeline uploaded successfully",
		"pipeline_id": pipelineID,
	})
}

// 触发流水线执行
func triggerPipeline(c *gin.Context) {
	pipelineID := c.Param("id")
	err := triggerPipelineByID(pipelineID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to trigger pipeline: " + err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "Pipeline triggered successfully"})
}

// 列出所有流水线
func listPipelines(c *gin.Context) {
	pipelines, err := getPipelinesFromDB()
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to get pipelines: " + err.Error()})
		return
	}
	c.JSON(http.StatusOK, pipelines)
}

// 获取流水线详情
func getPipeline(c *gin.Context) {
	pipelineID := c.Param("id")
	pipeline, err := getPipelineFromDB(pipelineID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to get pipeline: " + err.Error()})
		return
	}
	if pipeline == nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "Pipeline not found"})
		return
	}
	c.JSON(http.StatusOK, pipeline)
}

// 获取任务状态
func getTaskStatus(c *gin.Context) {
	taskID := c.Param("id")
	status, err := getTaskStatusFromDB(taskID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to get task status: " + err.Error()})
		return
	}
	if status == nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "Task not found"})
		return
	}
	c.JSON(http.StatusOK, status)
}
    