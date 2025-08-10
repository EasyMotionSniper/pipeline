package scheduler

import (
	"fmt"
	"log"
	"pace/internal/task_executor/runner"
	"pace/pkg/taskrpc"
	"sync"
)

// TaskScheduler 任务调度器，负责任务依赖管理和并发执行
type TaskScheduler struct {
	execEngine     *runner.DockerEngine
	maxConcurrency int
	statusCallback func(*taskrpc.TaskStatusUpdate) error
}

// taskExecutionContext 封装单次流水线执行的上下文信息，减少函数参数
type taskExecutionContext struct {
	depGraph    map[string][]string     // 任务依赖图
	taskMap     map[string]taskrpc.Task // 任务名到任务的映射
	taskStatus  map[string]string       // 任务状态(pending/running/success/failed)
	semaphore   chan struct{}           // 控制并发量的信号量
	wg          *sync.WaitGroup         // 等待所有任务完成
	statusMutex *sync.Mutex             // 保护taskStatus的互斥锁
	executeFunc func(taskrpc.Task)      // 递归执行任务的函数（自引用）
}

// NewTaskScheduler 创建新的任务调度器
func NewTaskScheduler(maxConcurrency int, statusCallback func(*taskrpc.TaskStatusUpdate) error) (*TaskScheduler, error) {
	execEngine, err := runner.NewDockerEngine()
	if err != nil {
		return nil, err
	}

	return &TaskScheduler{
		execEngine:     execEngine,
		maxConcurrency: maxConcurrency,
		statusCallback: statusCallback,
	}, nil
}

// SchedulePipeline 调度并执行整个流水线
func (s *TaskScheduler) SchedulePipeline(req *taskrpc.ExecutePipelineRequest) error {
	// 1. 初始化任务映射和依赖图
	taskMap := make(map[string]taskrpc.Task)
	for _, task := range req.Tasks {
		taskMap[task.Name] = task
	}

	depGraph, err := buildDependencyGraph(req.Tasks)
	if err != nil {
		return fmt.Errorf("构建依赖图失败: %v", err)
	}

	// 2. 初始化执行上下文（封装所有需要传递的参数）
	ctx := &taskExecutionContext{
		depGraph:    depGraph,
		taskMap:     taskMap,
		taskStatus:  make(map[string]string),
		semaphore:   make(chan struct{}, s.maxConcurrency),
		wg:          &sync.WaitGroup{},
		statusMutex: &sync.Mutex{},
	}

	// 初始化所有任务状态为pending
	for _, task := range req.Tasks {
		ctx.taskStatus[task.Name] = "pending"
		s.statusCallback(&taskrpc.TaskStatusUpdate{
			ExecutionID: req.ExecutionID,
			TaskName:    task.Name,
			Status:      "pending",
		})
	}

	// 3. 定义任务执行函数（通过上下文减少参数）
	ctx.executeFunc = func(task taskrpc.Task) {
		defer ctx.wg.Done()
		defer func() { <-ctx.semaphore }() // 释放并发名额

		// 更新任务状态为running
		ctx.statusMutex.Lock()
		ctx.taskStatus[task.Name] = "running"
		ctx.statusMutex.Unlock()

		s.statusCallback(&taskrpc.TaskStatusUpdate{
			ExecutionID: req.ExecutionID,
			TaskName:    task.Name,
			Status:      "running",
		})

		// 执行任务
		taskStatusUpdate, err := s.execEngine.ExecuteTask(task)

		// 确定任务最终状态
		status := "success"
		if err != nil {
			status = "failed"
		}

		// 执行结果检查（如果需要）
		if status == "success" && task.ResultCheck != "" {
			log.Printf("任务 %s 需要执行结果检查: %s", task.Name, task.ResultCheck)
			// TODO: 实现结果检查逻辑
		}

		// 更新任务最终状态
		ctx.statusMutex.Lock()
		ctx.taskStatus[task.Name] = status
		ctx.statusMutex.Unlock()

		// 通知状态更新
		s.statusCallback(&taskStatusUpdate)

		// 触发依赖任务执行
		s.executeDependentTasks(ctx, task.Name)
	}

	// 4. 启动初始任务（无依赖的任务）
	initialTasks := findInitialTasks(req.Tasks)
	for _, task := range initialTasks {
		ctx.semaphore <- struct{}{} // 获取并发名额
		ctx.wg.Add(1)
		go ctx.executeFunc(task)
	}

	// 等待所有任务完成
	ctx.wg.Wait()
	return nil
}

// executeDependentTasks 执行依赖于已完成任务的后续任务（通过上下文减少参数）
func (s *TaskScheduler) executeDependentTasks(ctx *taskExecutionContext, completedTaskName string) {
	// 遍历所有依赖于当前任务的后续任务
	for _, dependentTaskName := range ctx.depGraph[completedTaskName] {
		dependentTask, exists := ctx.taskMap[dependentTaskName]
		if !exists {
			log.Printf("警告: 依赖任务 %s 不存在，跳过", dependentTaskName)
			continue
		}

		// 检查任务是否仍处于待执行状态
		ctx.statusMutex.Lock()
		currentStatus := ctx.taskStatus[dependentTaskName]
		ctx.statusMutex.Unlock()

		if currentStatus != "pending" {
			log.Printf("任务 %s 已处于状态 %s，无需重复执行", dependentTaskName, currentStatus)
			continue
		}

		// 检查依赖是否满足，满足则执行
		if s.checkDependenciesSatisfied(ctx, dependentTask) {
			ctx.semaphore <- struct{}{}
			ctx.wg.Add(1)
			go ctx.executeFunc(dependentTask)
		}
	}
}

// checkDependenciesSatisfied 检查任务的所有依赖是否满足（通过上下文减少参数）
func (s *TaskScheduler) checkDependenciesSatisfied(ctx *taskExecutionContext, task taskrpc.Task) bool {
	ctx.statusMutex.Lock()
	defer ctx.statusMutex.Unlock()

	// 检查"依赖成功"的任务
	for _, depName := range task.DependsOnSuccess {
		status, exists := ctx.taskStatus[depName]
		if !exists || status != "success" {
			return false
		}
	}

	// 检查"依赖失败"的任务
	for _, depName := range task.DependsOnFailure {
		status, exists := ctx.taskStatus[depName]
		if !exists || status != "failed" {
			return false
		}
	}

	return true
}

// buildDependencyGraph 构建任务依赖图（任务 -> 依赖它的任务列表）
func buildDependencyGraph(tasks []taskrpc.Task) (map[string][]string, error) {
	graph := make(map[string][]string)
	taskNames := make(map[string]bool)

	// 初始化所有任务节点
	for _, task := range tasks {
		graph[task.Name] = []string{}
		if taskNames[task.Name] {
			return nil, fmt.Errorf("任务名称重复: %s", task.Name)
		}
		taskNames[task.Name] = true
	}

	// 构建依赖关系
	for _, task := range tasks {
		// 处理"依赖成功"的关系
		for _, depName := range task.DependsOnSuccess {
			if !taskNames[depName] {
				return nil, fmt.Errorf("任务 %s 依赖不存在的任务: %s", task.Name, depName)
			}
			graph[depName] = append(graph[depName], task.Name)
		}

		// 处理"依赖失败"的关系
		for _, depName := range task.DependsOnFailure {
			if !taskNames[depName] {
				return nil, fmt.Errorf("任务 %s 依赖不存在的任务: %s", task.Name, depName)
			}
			graph[depName] = append(graph[depName], task.Name)
		}
	}

	return graph, nil
}

// findInitialTasks 查找初始任务（无任何依赖的任务）
func findInitialTasks(tasks []taskrpc.Task) []taskrpc.Task {
	var initialTasks []taskrpc.Task
	for _, task := range tasks {
		if len(task.DependsOnSuccess) == 0 && len(task.DependsOnFailure) == 0 {
			initialTasks = append(initialTasks, task)
		}
	}
	return initialTasks
}
