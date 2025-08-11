package scheduler

import (
	"errors"
	"fmt"
	"log"
	"pace/internal/task_executor/runner"
	"pace/pkg/queue"
	"sync"
	"sync/atomic"

	"github.com/google/uuid"
)

type TaskStatusCallbackFunc func(*queue.TaskStatusUpdate) error
type PipelineStatusCallbackFunc func(*queue.PipelineStatusUpdate) error

type TaskScheduler struct {
	execEngine             *runner.DockerEngine
	maxConcurrency         int
	taskStatusCallback     TaskStatusCallbackFunc
	pipelineStatusCallback PipelineStatusCallbackFunc
}

// Task execution context with pipeline stop control
type taskExecutionContext struct {
	depGraph            map[string][]string
	taskMap             map[string]queue.Task
	taskStatus          map[string]string
	semaphore           chan struct{}
	wg                  *sync.WaitGroup
	statusMutex         *sync.Mutex
	stopFlag            int32
	pipelineExecuteUUID string
	executeFunc         func(queue.Task)
}

func NewTaskScheduler(maxConcurrency int, taskStatusCallback TaskStatusCallbackFunc, pipelineStatusCallback PipelineStatusCallbackFunc) (*TaskScheduler, error) {
	execEngine, err := runner.NewDockerEngine()
	if err != nil {
		return nil, err
	}

	return &TaskScheduler{
		execEngine:             execEngine,
		maxConcurrency:         maxConcurrency,
		taskStatusCallback:     taskStatusCallback,
		pipelineStatusCallback: pipelineStatusCallback,
	}, nil
}

// SchedulePipeline starts pipeline execution and stops entirely on any task failure
func (s *TaskScheduler) SchedulePipeline(req *queue.PipelineExecuteInfo) error {
	taskMap := make(map[string]queue.Task)
	for _, task := range req.Tasks {
		taskMap[task.Name] = task
	}

	depGraph, err := buildDependencyGraph(req.Tasks)
	if err != nil {
		return fmt.Errorf("failed to build dependency graph: %w", err)
	}
	pipelineExecuteUUID := uuid.New().String()
	// callback
	s.pipelineStatusCallback(&queue.PipelineStatusUpdate{
		PipelineVersionID:   req.PipelineVersionID,
		PipelineExecuteUUID: pipelineExecuteUUID,
		Status:              "running",
	})

	ctx := &taskExecutionContext{
		depGraph:            depGraph,
		taskMap:             taskMap,
		taskStatus:          make(map[string]string),
		semaphore:           make(chan struct{}, s.maxConcurrency),
		wg:                  &sync.WaitGroup{},
		statusMutex:         &sync.Mutex{},
		stopFlag:            0, // 0 means pipeline is running
		pipelineExecuteUUID: pipelineExecuteUUID,
	}

	// Initialize all tasks as pending
	for _, task := range req.Tasks {
		ctx.taskStatus[task.Name] = "pending"
		s.taskStatusCallback(&queue.TaskStatusUpdate{
			PipelineExecuteUUID: ctx.pipelineExecuteUUID,
			TaskName:            task.Name,
			Status:              "pending",
		})
	}

	// Define task execution logic with stop check
	ctx.executeFunc = func(task queue.Task) {
		// Check if pipeline is already stopped before starting
		if atomic.LoadInt32(&ctx.stopFlag) == 1 {
			return
		}

		// Mark task as running
		ctx.statusMutex.Lock()
		ctx.taskStatus[task.Name] = "running"
		ctx.statusMutex.Unlock()

		s.taskStatusCallback(&queue.TaskStatusUpdate{
			PipelineExecuteUUID: ctx.pipelineExecuteUUID,
			TaskName:            task.Name,
			Status:              "running",
		})

		// Execute task with stop check support
		taskResult, err := s.execEngine.ExecuteTask(task)

		status := taskResult.Status

		if err != nil {
			log.Printf("Task %s failed: %v", task.Name, err)
		}

		ctx.statusMutex.Lock()
		ctx.taskStatus[task.Name] = status
		ctx.statusMutex.Unlock()

		s.taskStatusCallback(&queue.TaskStatusUpdate{
			PipelineExecuteUUID: ctx.pipelineExecuteUUID,
			TaskName:            task.Name,
			Status:              status,
			Stdout:              taskResult.Stdout,
			Stderr:              taskResult.Stderr,
		})

		if status == "failed" {
			s.stopEntirePipeline(ctx)
			return
		}

		if atomic.LoadInt32(&ctx.stopFlag) == 0 {
			s.executeDependentTasks(ctx, task.Name)
		}
	}

	// Start initial tasks (no dependencies)
	initialTasks := findInitialTasks(req.Tasks)
	for _, task := range initialTasks {
		ctx.semaphore <- struct{}{}
		ctx.wg.Add(1)
		go func(t queue.Task) {
			defer ctx.wg.Done()
			defer func() { <-ctx.semaphore }()
			ctx.executeFunc(t)
		}(task)
	}

	ctx.wg.Wait()

	// Finalize pipeline status
	pipelineStatus := "success"
	if atomic.LoadInt32(&ctx.stopFlag) == 1 {
		pipelineStatus = "failed"
	}
	s.pipelineStatusCallback(&queue.PipelineStatusUpdate{
		PipelineVersionID:   req.PipelineVersionID,
		PipelineExecuteUUID: ctx.pipelineExecuteUUID,
		Status:              pipelineStatus,
	})
	return nil
}

// stopEntirePipeline stops all running tasks and marks all remaining tasks as skipped
func (s *TaskScheduler) stopEntirePipeline(ctx *taskExecutionContext) {
	if !atomic.CompareAndSwapInt32(&ctx.stopFlag, 0, 1) {
		return
	}
	log.Printf("Pipeline %s stopped due to task failure", ctx.pipelineExecuteUUID)

	ctx.statusMutex.Lock()
	defer ctx.statusMutex.Unlock()
	for taskName, status := range ctx.taskStatus {
		if status == "pending" {
			ctx.taskStatus[taskName] = "skipped"
			s.taskStatusCallback(&queue.TaskStatusUpdate{
				PipelineExecuteUUID: ctx.pipelineExecuteUUID,
				TaskName:            taskName,
				Status:              "skipped",
			})
			log.Printf("Task %s marked as skipped due to pipeline stop", taskName)
		}
	}
}

func (s *TaskScheduler) executeDependentTasks(ctx *taskExecutionContext, completedTaskName string) {
	if atomic.LoadInt32(&ctx.stopFlag) == 1 {
		return
	}

	for _, dependentTaskName := range ctx.depGraph[completedTaskName] {
		dependentTask, exists := ctx.taskMap[dependentTaskName]
		if !exists {
			log.Printf("Dependent task %s not found, skipping", dependentTaskName)
			continue
		}

		// Check if task is still pending
		ctx.statusMutex.Lock()
		currentStatus := ctx.taskStatus[dependentTaskName]
		ctx.statusMutex.Unlock()

		if currentStatus != "pending" {
			continue
		}

		// Check if all dependencies are satisfied
		if s.checkDependenciesSatisfied(ctx, dependentTask) {
			ctx.semaphore <- struct{}{}
			ctx.wg.Add(1)
			go func(t queue.Task) {
				defer ctx.wg.Done()
				defer func() { <-ctx.semaphore }()
				ctx.executeFunc(t)
			}(dependentTask)
		}
	}
}

func (s *TaskScheduler) checkDependenciesSatisfied(ctx *taskExecutionContext, task queue.Task) bool {
	ctx.statusMutex.Lock()
	defer ctx.statusMutex.Unlock()

	for _, depName := range task.DependsOn {
		status, exists := ctx.taskStatus[depName]
		if !exists || status != "success" {
			return false
		}
	}
	return true
}

func buildDependencyGraph(tasks []queue.Task) (map[string][]string, error) {
	graph := make(map[string][]string)
	taskNames := make(map[string]bool)

	for _, task := range tasks {
		graph[task.Name] = []string{}
		if taskNames[task.Name] {
			return nil, errors.New("duplicate task name: " + task.Name)
		}
		taskNames[task.Name] = true
	}

	for _, task := range tasks {
		for _, depName := range task.DependsOn {
			if !taskNames[depName] {
				return nil, fmt.Errorf("task %s depends on non-existent task %s", task.Name, depName)
			}
			graph[depName] = append(graph[depName], task.Name)
		}
	}

	return graph, nil
}

func findInitialTasks(tasks []queue.Task) []queue.Task {
	var initialTasks []queue.Task
	for _, task := range tasks {
		if len(task.DependsOn) == 0 {
			initialTasks = append(initialTasks, task)
		}
	}
	return initialTasks
}
