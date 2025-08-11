package test

import (
	"fmt"
	"pace/internal/task_executor/scheduler"
	"pace/pkg/queue"
	"testing"
)

func TestScheduler(t *testing.T) {
	pipelineCallback := func(update *queue.PipelineStatusUpdate) error {
		fmt.Printf("Pipeline %s status updated to %s\n", update.PipelineExecuteUUID, update.Status)
		return nil
	}
	taskStatusCallback := func(update *queue.TaskStatusUpdate) error {
		fmt.Printf("Task %s status updated to %s\n", update.TaskName, update.Status)
		return nil
	}
	scheduler, err := scheduler.NewTaskScheduler(10, taskStatusCallback, pipelineCallback)
	if err != nil {
		panic(err)
	}
	scheduler.SchedulePipeline(&queue.PipelineExecuteInfo{
		PipelineVersionID: 1,
		Tasks: []queue.Task{
			{Name: "task1", Command: "echo Task 1"},
			{Name: "task2", Command: "echo Task 2 && sleep 3", DependsOn: []string{"task1"}},
			{Name: "task3", Command: "echo Task 3", DependsOn: []string{"task2"}},
			{Name: "task4", Command: "echo Task 4", DependsOn: []string{"task3"}},
		},
	})
}
