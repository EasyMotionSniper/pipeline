package main

import (
	"errors"
	"fmt"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// Task 表示一个需要在Docker容器中执行的任务
type Task struct {
	ID           string   // 任务唯一标识
	Name         string   // 任务名称
	Command      string   // 要执行的命令
	Dependencies []string // 依赖的任务ID列表
	Image        string   // Docker镜像，默认为alpine
	Status       string   // 任务状态：pending, running, success, failed
	ContainerID  string   // 运行任务的容器ID
	Output       string   // 命令输出
	Error        string   // 错误信息
}

// Scheduler 任务调度器
type Scheduler struct {
	tasks     map[string]*Task    // 任务映射：ID -> Task
	depGraph  map[string][]string // 依赖图：任务ID -> 依赖它的任务列表
	mu        sync.Mutex
	errChan   chan error      // 用于传递任务执行错误的通道
	completed map[string]bool // 记录已完成的任务
}

// NewScheduler 创建新的调度器
func NewScheduler() *Scheduler {
	return &Scheduler{
		tasks:     make(map[string]*Task),
		depGraph:  make(map[string][]string),
		errChan:   make(chan error, 1), // 缓冲通道，避免阻塞
		completed: make(map[string]bool),
	}
}

// AddTask 添加任务到调度器
func (s *Scheduler) AddTask(task *Task) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.tasks[task.ID]; exists {
		return fmt.Errorf("task %s already exists", task.ID)
	}

	// 验证依赖的任务是否存在
	for _, depID := range task.Dependencies {
		if _, exists := s.tasks[depID]; !exists {
			return fmt.Errorf("task %s depends on non-existent task %s", task.ID, depID)
		}
		// 构建反向依赖图：记录哪些任务依赖当前任务
		s.depGraph[depID] = append(s.depGraph[depID], task.ID)
	}

	// 设置默认镜像
	if task.Image == "" {
		task.Image = "alpine:latest"
	}

	task.Status = "pending"
	s.tasks[task.ID] = task
	return nil
}

// 计算每个任务的入度（依赖的任务数）
func (s *Scheduler) calculateInDegree() map[string]int {
	inDegree := make(map[string]int)
	for id, task := range s.tasks {
		inDegree[id] = len(task.Dependencies)
	}
	return inDegree
}

// Run 并行执行所有任务（按依赖关系分组并行）
func (s *Scheduler) Run() error {
	inDegree := s.calculateInDegree()
	var wg sync.WaitGroup

	// 初始化可执行任务队列（入度为0的任务）
	queue := make([]string, 0)
	for id, degree := range inDegree {
		if degree == 0 {
			queue = append(queue, id)
		}
	}

	// 如果没有可执行任务且存在任务，说明有循环依赖
	if len(queue) == 0 && len(s.tasks) > 0 {
		return errors.New("circular dependency detected in tasks")
	}

	fmt.Println("Starting parallel task execution...")

	// 处理任务队列的函数
	processQueue := func() {
		defer wg.Done()

		for len(queue) > 0 {
			// 检查是否有错误发生，有则退出
			select {
			case err := <-s.errChan:
				fmt.Printf("Fatal error: %v\n", err)
				return
			default:
			}

			// 一次取出当前队列中所有可执行的任务（并行执行这一批）
			currentBatch := make([]string, len(queue))
			copy(currentBatch, queue)
			queue = queue[:0] // 清空队列，准备接收下一批

			var batchWg sync.WaitGroup
			batchWg.Add(len(currentBatch))

			fmt.Printf("Starting batch execution: %v\n", currentBatch)

			// 并行执行当前批次的任务
			for _, taskID := range currentBatch {
				go func(id string) {
					defer batchWg.Done()

					// 检查是否已收到错误，有则不执行
					select {
					case err := <-s.errChan:
						fmt.Printf("Task %s skipped due to error: %v\n", id, err)
						return
					default:
					}

					task := s.tasks[id]
					if err := s.runTask(task); err != nil {
						// 发送错误到通道（只发送第一个错误）
						select {
						case s.errChan <- err:
						default:
						}
						return
					}

					// 任务完成后，更新依赖它的任务的入度
					s.mu.Lock()
					s.completed[id] = true
					for _, dependentID := range s.depGraph[id] {
						inDegree[dependentID]--
						if inDegree[dependentID] == 0 {
							queue = append(queue, dependentID)
						}
					}
					s.mu.Unlock()
				}(taskID)
			}

			// 等待当前批次所有任务完成
			batchWg.Wait()
			fmt.Printf("Completed batch: %v\n", currentBatch)
		}
	}

	wg.Add(1)
	go processQueue()
	wg.Wait()

	// 检查是否有未完成的任务
	s.mu.Lock()
	defer s.mu.Unlock()
	for id, task := range s.tasks {
		if !s.completed[id] && task.Status != "failed" {
			return fmt.Errorf("task %s was not executed", id)
		}
	}

	// 检查是否有错误
	select {
	case err := <-s.errChan:
		return err
	default:
		return nil
	}
}

// 执行单个任务（在Docker容器中）
func (s *Scheduler) runTask(task *Task) error {
	s.mu.Lock()
	task.Status = "running"
	s.mu.Unlock()

	defer func() {
		s.mu.Lock()
		if task.Status != "success" {
			task.Status = "failed"
		}
		s.mu.Unlock()
	}()

	// 1. 创建并启动Docker容器
	containerID, err := createDockerContainer(task.Image)
	if err != nil {
		task.Error = fmt.Sprintf("Failed to create container: %v", err)
		return errors.New(task.Error)
	}

	// 记录容器ID
	s.mu.Lock()
	task.ContainerID = containerID
	s.mu.Unlock()

	fmt.Printf("Task %s running in container: %s\n", task.ID, containerID[:12])

	// 2. 在容器中执行命令
	output, err := executeInDocker(containerID, task.Command)

	// 3. 记录执行结果
	outputStr := string(output)
	s.mu.Lock()
	task.Output = outputStr
	s.mu.Unlock()

	if err != nil {
		// 命令执行失败，但仍尝试清理容器
		cleanupDockerContainer(containerID)
		task.Error = fmt.Sprintf("Command execution failed: %v\nOutput: %s", err, outputStr)
		return errors.New(task.Error)
	}

	// 4. 清理容器
	if err := cleanupDockerContainer(containerID); err != nil {
		fmt.Printf("Warning: Failed to clean up container %s: %v\n", containerID[:12], err)
	}

	// 5. 标记任务成功
	s.mu.Lock()
	task.Status = "success"
	s.mu.Unlock()
	fmt.Printf("Task %s completed successfully\nOutput: %s\n", task.ID, outputStr)
	return nil
}

// 创建Docker容器
func createDockerContainer(image string) (string, error) {
	// 检查镜像是否存在，不存在则拉取
	if !dockerImageExists(image) {
		fmt.Printf("Pulling Docker image: %s\n", image)
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

// 检查Docker是否可用
func isDockerAvailable() bool {
	cmd := exec.Command("docker", "info")
	return cmd.Run() == nil
}
