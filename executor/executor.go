package executor

import (
	"context"
	"io"
	"os"
	"time"

	"yourproject/models"

	"github.com/docker/docker/api/types"
	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/client"
	"github.com/docker/docker/pkg/stdcopy"
)

// TaskResult 表示任务执行结果
type TaskResult struct {
	TaskID    uint
	TaskName  string
	Status    string
	Stdout    string
	Stderr    string
	StartTime time.Time
	EndTime   time.Time
}

// Executor 任务执行器
type Executor struct {
	dockerClient   *client.Client
	resultChan     chan TaskResult
	maxConcurrency int
	semaphore      chan struct{}
}

// NewExecutor 创建新的任务执行器
func NewExecutor(maxConcurrency int) (*Executor, error) {
	cli, err := client.NewClientWithOpts(client.FromEnv)
	if err != nil {
		return nil, err
	}

	return &Executor{
		dockerClient:   cli,
		resultChan:     make(chan TaskResult, 100),
		maxConcurrency: maxConcurrency,
		semaphore:      make(chan struct{}, maxConcurrency),
	}, nil
}

// ResultChan 返回结果通道
func (e *Executor) ResultChan() <-chan TaskResult {
	return e.resultChan
}

// ExecuteTask 执行任务
func (e *Executor) ExecuteTask(task models.Task) {
	// 使用信号量控制并发
	e.semaphore <- struct{}{}
	defer func() { <-e.semaphore }()

	result := TaskResult{
		TaskID:    task.ID,
		TaskName:  task.Name,
		StartTime: time.Now(),
	}

	// 创建容器
	ctx := context.Background()
	resp, err := e.dockerClient.ContainerCreate(ctx, &container.Config{
		Image: "alpine:latest",
		Cmd:   []string{"/bin/sh", "-c", task.Command},
		Tty:   false,
	}, nil, nil, nil, "")

	if err != nil {
		result.Status = "failed"
		result.Stderr = err.Error()
		result.EndTime = time.Now()
		e.resultChan <- result
		return
	}
	containerID := resp.ID

	// 确保容器最终被删除
	defer func() {
		e.dockerClient.ContainerRemove(ctx, containerID, types.ContainerRemoveOptions{})
	}()

	// 启动容器
	if err := e.dockerClient.ContainerStart(ctx, containerID, types.ContainerStartOptions{}); err != nil {
		result.Status = "failed"
		result.Stderr = err.Error()
		result.EndTime = time.Now()
		e.resultChan <- result
		return
	}

	// 等待容器完成
	statusCh, errCh := e.dockerClient.ContainerWait(ctx, containerID, container.WaitConditionNotRunning)
	select {
	case err := <-errCh:
		if err != nil {
			result.Status = "failed"
			result.Stderr = err.Error()
			result.EndTime = time.Now()
			e.resultChan <- result
			return
		}
	case <-statusCh:
	}

	// 获取容器日志
	logs, err := e.dockerClient.ContainerLogs(ctx, containerID, types.ContainerLogsOptions{
		ShowStdout: true,
		ShowStderr: true,
	})
	if err != nil {
		result.Status = "failed"
		result.Stderr = err.Error()
		result.EndTime = time.Now()
		e.resultChan <- result
		return
	}
	defer logs.Close()

	// 分离stdout和stderr
	var stdout, stderr io.ReadCloser
	stdout, stderr = stdcopy.StdCopy(os.Stdout, os.Stderr, logs)

	stdoutBytes, _ := io.ReadAll(stdout)
	stderrBytes, _ := io.ReadAll(stderr)

	result.Stdout = string(stdoutBytes)
	result.Stderr = string(stderrBytes)

	// 如果有结果检查命令，执行检查
	if task.ResultCheck != "" {
		checkResult, err := e.executeCommandInContainer(ctx, containerID, task.ResultCheck)
		if err != nil || checkResult.Status != "success" {
			result.Status = "failed"
			result.Stderr += "\nResult check failed: " + checkResult.Stderr
			result.EndTime = time.Now()
			e.resultChan <- result
			return
		}
	}

	result.Status = "success"
	result.EndTime = time.Now()
	e.resultChan <- result
}

// 在已有的容器中执行命令（用于结果检查）
func (e *Executor) executeCommandInContainer(ctx context.Context, containerID, command string) (TaskResult, error) {
	result := TaskResult{
		StartTime: time.Now(),
	}

	execResp, err := e.dockerClient.ContainerExecCreate(ctx, containerID, types.ExecConfig{
		Cmd:          []string{"/bin/sh", "-c", command},
		Tty:          false,
		AttachStdout: true,
		AttachStderr: true,
	})
	if err != nil {
		return result, err
	}

	resp, err := e.dockerClient.ContainerExecAttach(ctx, execResp.ID, types.ExecStartCheck{})
	if err != nil {
		return result, err
	}
	defer resp.Close()

	// 分离stdout和stderr
	var stdout, stderr io.ReadCloser
	stdout, stderr = stdcopy.StdCopy(os.Stdout, os.Stderr, resp.Reader)

	stdoutBytes, _ := io.ReadAll(stdout)
	stderrBytes, _ := io.ReadAll(stderr)

	result.Stdout = string(stdoutBytes)
	result.Stderr = string(stderrBytes)

	// 检查执行状态
	inspect, err := e.dockerClient.ContainerExecInspect(ctx, execResp.ID)
	if err != nil {
		return result, err
	}

	if inspect.ExitCode == 0 {
		result.Status = "success"
	} else {
		result.Status = "failed"
	}

	result.EndTime = time.Now()
	return result, nil
}
