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
	tasks map[string]*Task // 任务映射：ID -> Task
	mu    sync.Mutex       // 同步锁
}

// NewScheduler 创建新的调度器
func NewScheduler() *Scheduler {
	return &Scheduler{
		tasks: make(map[string]*Task),
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
	}

	// 设置默认镜像
	if task.Image == "" {
		task.Image = "docker.1ms.run/alpine"
	}

	task.Status = "pending"
	s.tasks[task.ID] = task
	return nil
}

// 检测依赖环并生成拓扑排序后的任务执行顺序
func (s *Scheduler) getExecutionOrder() ([]*Task, error) {
	// 构建依赖图：任务ID -> 依赖的任务列表
	depGraph := make(map[string][]string)
	inDegree := make(map[string]int) // 每个任务的入度（依赖的任务数）
	taskList := make([]*Task, 0, len(s.tasks))

	// 初始化依赖图和入度
	for _, task := range s.tasks {
		taskList = append(taskList, task)
		depGraph[task.ID] = task.Dependencies
		inDegree[task.ID] = len(task.Dependencies)
	}

	// 拓扑排序（Kahn算法）
	queue := make([]string, 0)
	for id, degree := range inDegree {
		if degree == 0 {
			queue = append(queue, id)
		}
	}

	result := make([]*Task, 0)
	for len(queue) > 0 {
		currentID := queue[0]
		queue = queue[1:]

		// 找到当前任务
		var currentTask *Task
		for _, t := range taskList {
			if t.ID == currentID {
				currentTask = t
				break
			}
		}
		result = append(result, currentTask)

		// 减少依赖当前任务的任务的入度
		for _, t := range taskList {
			for _, depID := range t.Dependencies {
				if depID == currentID {
					inDegree[t.ID]--
					if inDegree[t.ID] == 0 {
						queue = append(queue, t.ID)
					}
				}
			}
		}
	}

	// 检查是否有循环依赖（未处理完所有任务）
	if len(result) != len(s.tasks) {
		return nil, errors.New("circular dependency detected in tasks")
	}

	return result, nil
}

// Run 执行所有任务（按依赖顺序）
func (s *Scheduler) Run() error {
	// 获取执行顺序
	executionOrder, err := s.getExecutionOrder()
	if err != nil {
		return err
	}

	fmt.Printf("Execution order: %v\n", getTaskIDs(executionOrder))

	// 依次执行任务
	for _, task := range executionOrder {
		if err := s.runTask(task); err != nil {
			fmt.Printf("Task %s failed: %v\n", task.ID, err)
			// 依赖该任务的后续任务将无法执行，直接返回
			return err
		}
	}

	return nil
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

	// 4. 清理容器（如果需要保留容器用于调试，可以注释掉这行）
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

// 辅助函数：获取任务ID列表
func getTaskIDs(tasks []*Task) []string {
	ids := make([]string, len(tasks))
	for i, t := range tasks {
		ids[i] = t.ID
	}
	return ids
}

// 检查Docker是否可用
func isDockerAvailable() bool {
	cmd := exec.Command("docker", "info")
	return cmd.Run() == nil
}
