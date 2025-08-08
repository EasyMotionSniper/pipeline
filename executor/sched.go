package executor

import (
	"sync"
	"time"

	"yourproject/db"
	"yourproject/executor"
	"yourproject/models"

	"github.com/robfig/cron/v3"
)

// Scheduler 任务调度器
type Scheduler struct {
	executor     *executor.Executor
	cron         *cron.Cron
	pipelineJobs map[string]cron.EntryID // 存储pipeline的cron任务ID
	mu           sync.Mutex
}

// NewScheduler 创建新的调度器
func NewScheduler(exec *executor.Executor) *Scheduler {
	return &Scheduler{
		executor:     exec,
		cron:         cron.New(),
		pipelineJobs: make(map[string]cron.EntryID),
	}
}

// Start 启动调度器
func (s *Scheduler) Start() {
	s.cron.Start()
	go s.handleTaskResults()
}

// Stop 停止调度器
func (s *Scheduler) Stop() {
	s.cron.Stop()
}

// SchedulePipeline 为流水线设置调度
func (s *Scheduler) SchedulePipeline(pipeline *models.Pipeline) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// 先移除已有的调度
	if jobID, exists := s.pipelineJobs[pipeline.Name]; exists {
		s.cron.Remove(jobID)
	}

	// 添加新的cron调度
	for _, trigger := range pipeline.Triggers {
		if trigger.Cron != "" {
			job := NewPipelineJob(pipeline.Name, s.executor)
			entryID, err := s.cron.AddJob(trigger.Cron, job)
			if err != nil {
				return err
			}
			s.pipelineJobs[pipeline.Name] = entryID
			break // 目前只支持一个cron表达式
		}
	}

	return nil
}

// TriggerPipeline 手动触发流水线执行
func (s *Scheduler) TriggerPipeline(pipelineName string, triggerType string) error {
	pipeline, err := db.GetLatestPipeline(pipelineName)
	if err != nil {
		return err
	}

	return s.executePipeline(pipeline, triggerType)
}

// 执行流水线
func (s *Scheduler) executePipeline(pipeline *models.Pipeline, triggerType string) error {
	// 创建流水线执行记录
	exec := &models.PipelineExecution{
		PipelineID:  pipeline.ID,
		PipelineVer: pipeline.Version,
		TriggerType: triggerType,
		Status:      "running",
		StartTime:   time.Now(),
	}
	if err := db.CreatePipelineExecution(exec); err != nil {
		return err
	}

	// 构建任务依赖图
	depGraph := buildDependencyGraph(pipeline.Tasks)

	// 找出所有没有依赖的初始任务
	var initialTasks []models.Task
	for _, task := range pipeline.Tasks {
		if len(task.DependsOnSuccess) == 0 && len(task.DependsOnFailure) == 0 {
			initialTasks = append(initialTasks, task)
		}
	}

	// 为每个任务创建执行记录
	taskExecMap := make(map[string]*models.TaskExecution)
	for _, task := range pipeline.Tasks {
		taskExec := &models.TaskExecution{
			PipelineExecID: exec.ID,
			TaskID:         task.ID,
			TaskName:       task.Name,
			Status:         "pending",
		}
		if err := db.CreateTaskExecution(taskExec); err != nil {
			return err
		}
		taskExecMap[task.Name] = taskExec
	}

	// 启动任务执行协调器
	go s.coordinateTaskExecution(pipeline.Tasks, depGraph, taskExecMap, exec.ID)

	// 执行初始任务
	for _, task := range initialTasks {
		go s.executor.ExecuteTask(task)
	}

	return nil
}

// 构建任务依赖图
func buildDependencyGraph(tasks []models.Task) map[string][]string {
	graph := make(map[string][]string)

	// 初始化所有任务
	for _, task := range tasks {
		graph[task.Name] = []string{}
	}

	// 构建依赖关系：key依赖于value完成
	for _, task := range tasks {
		for _, dep := range task.DependsOnSuccess {
			graph[dep] = append(graph[dep], task.Name)
		}
		for _, dep := range task.DependsOnFailure {
			graph[dep] = append(graph[dep], task.Name)
		}
	}

	return graph
}

// 协调任务执行，处理依赖关系
func (s *Scheduler) coordinateTaskExecution(
	tasks []models.Task,
	depGraph map[string][]string,
	taskExecMap map[string]*models.TaskExecution,
	pipelineExecID uint,
) {
	// 跟踪任务状态
	taskStatus := make(map[string]string)
	for _, task := range tasks {
		taskStatus[task.Name] = "pending"
	}

	// 计算总任务数
	totalTasks := len(tasks)
	completedTasks := 0

	// 等待所有任务完成
	for completedTasks < totalTasks {
		// 检查是否有新的任务结果
		// 实际实现中应该使用通道或其他机制等待结果
		time.Sleep(1 * time.Second)

		// 更新任务状态
		for name, exec := range taskExecMap {
			if taskStatus[name] != exec.Status &&
				(exec.Status == "success" || exec.Status == "failed") {

				taskStatus[name] = exec.Status
				completedTasks++

				// 检查依赖于此任务的后续任务
				for _, dependent := range depGraph[name] {
					// 检查依赖是否满足
					if s.checkDependenciesSatisfied(dependent, tasks, taskStatus) {
						// 找到对应的任务并执行
						for _, task := range tasks {
							if task.Name == dependent && taskStatus[dependent] == "pending" {
								taskStatus[dependent] = "running"
								exec := taskExecMap[dependent]
								exec.Status = "running"
								exec.StartTime = time.Now()
								db.UpdateTaskExecution(exec)

								go s.executor.ExecuteTask(task)
								break
							}
						}
					}
				}
			}
		}
	}

	// 所有任务完成，更新流水线状态
	pipelineExec := &models.PipelineExecution{}
	db.DB.First(pipelineExec, pipelineExecID)

	// 检查是否有失败的任务
	hasFailed := false
	for _, status := range taskStatus {
		if status == "failed" {
			hasFailed = true
			break
		}
	}

	if hasFailed {
		pipelineExec.Status = "failed"
	} else {
		pipelineExec.Status = "completed"
	}
	pipelineExec.EndTime = time.Now()
	db.UpdatePipelineExecution(pipelineExec)
}

// 检查任务的依赖是否满足
func (s *Scheduler) checkDependenciesSatisfied(taskName string, tasks []models.Task, taskStatus map[string]string) bool {
	// 找到任务
	var task models.Task
	for _, t := range tasks {
		if t.Name == taskName {
			task = t
			break
		}
	}

	// 检查依赖成功的任务
	for _, dep := range task.DependsOnSuccess {
		if taskStatus[dep] != "success" {
			return false
		}
	}

	// 检查依赖失败的任务
	for _, dep := range task.DependsOnFailure {
		if taskStatus[dep] != "failed" {
			return false
		}
	}

	return true
}

// 处理任务执行结果
func (s *Scheduler) handleTaskResults() {
	for result := range s.executor.ResultChan() {
		// 查找对应的任务执行记录
		var taskExec models.TaskExecution
		result := db.DB.Where("task_id = ? AND status = 'running'", result.TaskID).First(&taskExec)
		if result.Error != nil {
			continue
		}

		// 更新任务执行记录
		taskExec.Status = result.Status
		taskExec.Stdout = result.Stdout
		taskExec.Stderr = result.Stderr
		taskExec.StartTime = result.StartTime
		taskExec.EndTime = result.EndTime
		db.UpdateTaskExecution(&taskExec)
	}
}

// PipelineJob 用于cron调度的流水线任务
type PipelineJob struct {
	pipelineName string
	executor     *executor.Executor
}

// NewPipelineJob 创建新的流水线定时任务
func NewPipelineJob(pipelineName string, executor *executor.Executor) *PipelineJob {
	return &PipelineJob{
		pipelineName: pipelineName,
		executor:     executor,
	}
}

// Run 执行定时任务
func (j *PipelineJob) Run() {
	scheduler := &Scheduler{executor: j.executor}
	scheduler.TriggerPipeline(j.pipelineName, "cron")
}
