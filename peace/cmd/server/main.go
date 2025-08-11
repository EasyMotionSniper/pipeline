package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"pace/internal/server/dao"
	"pace/internal/server/handler"
	"pace/internal/server/model"
	"pace/internal/server/scheduler"
	"pace/pkg/queue"

	"github.com/gin-gonic/gin"
	"github.com/hibiken/asynq"
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

	srv := asynq.NewServer(asynq.RedisClientOpt{
		Addr:     "localhost:6379",
		Password: "justredis",
	}, asynq.Config{
		Concurrency: 10,
	})

	srv.Start(asynq.HandlerFunc(func(ctx context.Context, t *asynq.Task) error {
		switch t.Type() {
		case queue.PIPELINE_STATUS_UPDATE:
			var update queue.PipelineStatusUpdate
			if err := json.Unmarshal(t.Payload(), &update); err != nil {
				return err
			}
			if err := dao.NewPipelineExecDao().Upsert(ctx, &model.PipelineExecution{
				PipelineExecuteUUID: update.PipelineExecuteUUID,
				Status:              update.Status,
			}); err != nil {
				fmt.Printf("Failed to update pipeline status: %v\n", err)
				return err
			}
			return nil
		case queue.TASK_STATUS_UPDATE:
			var update queue.TaskStatusUpdate
			if err := json.Unmarshal(t.Payload(), &update); err != nil {
				return err
			}
			if err := dao.NewTaskExecDao().Upsert(ctx, &model.TaskExecution{
				PipelineExecuteUUID: update.PipelineExecuteUUID,
				TaskName:            update.TaskName,
				Status:              update.Status,
				Stdout:              update.Stdout,
				Stderr:              update.Stderr,
			}); err != nil {
				fmt.Printf("Failed to update task status: %v\n", err)
				return err
			}
			return nil
		default:
			return fmt.Errorf("unknown task type: %s", t.Type())
		}
	}))

	if err := scheduler.StartScheduler(); err != nil {
		log.Fatalf("Failed to start scheduler: %v", err)
	}

	r.Run(":8080")

}
