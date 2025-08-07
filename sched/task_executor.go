package main

import (
	"context"
	"fmt"
	"log"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// TaskExecutor 任务执行器，负责在Docker容器中执行任务
type TaskExecutor struct {
	maxConcurrency int
	taskQueue      chan Task
	wg             sync.WaitGroup
	ctx            context.Context
	cancel         context.CancelFunc
}

// NewTaskExecutor 创建新的任务执行器
func NewTaskExecutor(maxConcurrency int) *TaskExecutor {
	ctx, cancel := context.WithCancel(context.Background())
	executor := &TaskExecutor{
		maxConcurrency: maxConcurrency,
		taskQueue:      make(chan Task, 100),
		ctx:            ctx,
		cancel:         cancel,
	}

	// 启动工作协程
	for i := 0; i < maxConcurrency; i++ {
		executor.wg.Add(1)
		go executor.worker(i)
	}

	return executor
}

// SubmitTask 提交任务到执行队列
func (e *TaskExecutor) SubmitTask(task Task) {
	select {
	case e.taskQueue <- task:
		log.Printf("Task %s submitted to queue", task.ID)
	case <-e.ctx.Done():
		log.Printf("Task executor is shutting down, cannot submit task %s", task.ID)
	}
}

// Stop 停止任务执行器
func (e *TaskExecutor) Stop() {
	close(e.taskQueue)
	e.cancel()
	e.wg.Wait()
	log.Println("Task executor stopped")
}

// worker 工作协程，处理任务队列中的任务
func (e *TaskExecutor) worker(id int) {
	defer e.wg.Done()
	log.Printf("Worker %d started", id)

	for task := range e.taskQueue {
		select {
		case <-e.ctx.Done():
			log.Printf("Worker %d shutting down", id)
			return
		default:
			log.Printf("Worker %d processing task %s", id, task.ID)
			e.executeTask(task)
		}
	}

	log.Printf("Worker %d exiting", id)
}

// executeTask 在Docker容器中执行任务
func (e *TaskExecutor) executeTask(task Task) {
	// 发送任务开始状态
	taskStatusChan <- TaskStatusUpdate{
		TaskID:     task.ID,
		PipelineID: task.PipelineID,
		Status:     TaskStatusRunning,
	}

	// 创建容器并执行命令
	containerID, err := createDockerContainer(task.Image)
	if err != nil {
		log.Printf("Failed to create container for task %s: %v", task.ID, err)
		taskStatusChan <- TaskStatusUpdate{
			TaskID:     task.ID,
			PipelineID: task.PipelineID,
			Status:     TaskStatusFailed,
			Stderr:     fmt.Sprintf("Failed to create container: %v", err),
			CompletedAt: time.Now(),
		}
		return
	}
	log.Printf("Task %s running in container: %s", task.ID, containerID[:12])

	// 在容器中执行命令
	output, err := executeInDocker(containerID, task.Command)
	outputStr := string(output)
	
	// 清理容器
	cleanupErr := cleanupDockerContainer(containerID)
	if cleanupErr != nil {
		log.Printf("Warning: Failed to clean up container %s: %v", containerID[:12], cleanupErr)
	}

	// 发送任务完成状态
	update := TaskStatusUpdate{
		TaskID:     task.ID,
		PipelineID: task.PipelineID,
		CompletedAt: time.Now(),
	}

	if err != nil {
		update.Status = TaskStatusFailed
		update.Stderr = fmt.Sprintf("Command execution failed: %v\nOutput: %s", err, outputStr)
		log.Printf("Task %s failed: %v", task.ID, err)
	} else {
		update.Status = TaskStatusSuccess
		update.Stdout = outputStr
		log.Printf("Task %s completed successfully", task.ID)
	}

	taskStatusChan <- update
}

// 创建Docker容器
func createDockerContainer(image string) (string, error) {
	// 检查镜像是否存在，不存在则拉取
	if !dockerImageExists(image) {
		log.Printf("Pulling Docker image: %s", image)
		cmd := exec.Command("docker", "pull", image)
		output, err := cmd.CombinedOutput()
		if err != nil {
			return "", fmt.Errorf("failed to pull image %s: %v\nOutput: %s", image, err, string(output))
		}
	}

	// 创建并启动容器（后台运行）
	cmd := exec.Command("docker", "run", "-d", "--rm", image, "sleep", "3600")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("docker run failed: %v\nOutput: %s", err, string(output))
	}

	// 容器ID是命令输出的前12个字符（完整ID可能带换行符）
	containerID := strings.TrimSpace(string(output))
	if len(containerID) < 12 {
		return "", fmt.Errorf("invalid container ID: %s", containerID)
	}

	// 等待容器启动
	time.Sleep(500 * time.Millisecond)
	return containerID, nil
}

// 在Docker容器中执行命令
func executeInDocker(containerID, command string) ([]byte, error) {
	// 使用sh -c来支持复杂命令和管道
	cmd := exec.Command("docker", "exec", containerID, "sh", "-c", command)
	return cmd.CombinedOutput()
}

// 清理Docker容器
func cleanupDockerContainer(containerID string) error {
	cmd := exec.Command("docker", "stop", containerID)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to stop container: %v\nOutput: %s", err, string(output))
	}
	return nil
}

// 检查Docker镜像是否已存在
func dockerImageExists(image string) bool {
	cmd := exec.Command("docker", "images", "-q", image)
	output, err := cmd.CombinedOutput()
	return err == nil && strings.TrimSpace(string(output)) != ""
}
    