package main

import (
	"context"
	"encoding/json"
	"fmt"
	"pace/internal/common"
	"pace/internal/task_executor/scheduler"
	"pace/pkg/queue"

	"github.com/hibiken/asynq"
)

func main() {
	config := common.GetConfig()
	srv := asynq.NewServer(
		asynq.RedisClientOpt{Addr: config.RedisAddr},
		asynq.Config{
			Concurrency: 10, // number of concurrent workers
		},
	)

	client := asynq.NewClient(asynq.RedisClientOpt{Addr: config.RedisAddr})
	pipelineCallback := func(update *queue.PipelineStatusUpdate) error {
		data, err := json.Marshal(update)
		if err != nil {
			return err
		}
		if _, err := client.Enqueue(asynq.NewTask(queue.PIPELINE_STATUS_UPDATE, []byte(data))); err != nil {
			return err
		}
		return nil
	}
	taskStatusCallback := func(update *queue.TaskStatusUpdate) error {
		data, err := json.Marshal(update)
		if err != nil {
			return err
		}
		if _, err := client.Enqueue(asynq.NewTask(queue.TASK_STATUS_UPDATE, []byte(data))); err != nil {
			return err
		}
		return nil
	}

	scheduler, err := scheduler.NewTaskScheduler(10, taskStatusCallback, pipelineCallback)
	if err != nil {
		panic(err)
	}

	srv.Run(asynq.HandlerFunc(func(ctx context.Context, t *asynq.Task) error {
		switch t.Type() {
		case queue.PIPELINE_EXECUTE:
			var req queue.PipelineExecuteInfo
			if err := json.Unmarshal(t.Payload(), &req); err != nil {
				return fmt.Errorf("failed to unmarshal PipelineExecuteInfo: %w", err)
			}
			fmt.Printf("Received pipeline execute task: %v\n", req)
			return scheduler.SchedulePipeline(&req)
		default:
			fmt.Printf("Received unknown task type: %s\n", t.Type())
			return fmt.Errorf("unknown task type: %s", t.Type())
		}

	}))
}
