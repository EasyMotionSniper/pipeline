
系统架构

graph TD
    A[Client] -->|上传YAML| B(Gin API)
    B --> C{解析YAML}
    C --> D[任务依赖图谱]
    D --> E[Cron调度器]
    D --> F[Webhook路由]
    E --> G[任务队列]
    F --> G
    G --> H[Task Executor]
    H --> I[Docker容器]
    I --> J[结果Channel]
    J --> K[状态更新]


核心模块实现

1. YAML 任务定义

pipeline:
  id: data_pipeline
  triggers:
    - type: cron
      expression: "0 */2 * * *"  # 每2小时执行
    - type: webhook
      endpoint: /trigger/data_pipeline
  tasks:
    - id: task_1
      image: python:3.9
      command: ["python", "process.py"]
      depends_on: []
    - id: task_2
      image: node:18
      command: ["node", "validate.js"]
      depends_on: ["task_1"]  # 依赖 task_1


2. Gin 路由与 YAML 解析（api/handler.go）

// 定义 YAML 结构体
type TaskDef struct {
    ID        string   `yaml:"id"`
    Image     string   `yaml:"image"`
    Command   []string `yaml:"command"`
    DependsOn []string `yaml:"depends_on"`
}

type PipelineDef struct {
    Triggers []struct {
        Type       string `yaml:"type"`
        Expression string `yaml:"expression,omitempty"`
        Endpoint   string `yaml:"endpoint,omitempty"`
    } `yaml:"triggers"`
    Tasks []TaskDef `yaml:"tasks"`
}

// 接收 YAML 上传
func UploadPipeline(c *gin.Context) {
    file, _ := c.FormFile("file")
    f, _ := file.Open()
    defer f.Close()

    var pipelineDef PipelineDef
    if err := yaml.NewDecoder(f).Decode(&pipelineDef); err != nil {
        c.JSON(400, gin.H{"error": "YAML解析失败"})
        return
    }

    // 存储到数据库并初始化调度
    pipeline := saveToDB(pipelineDef)
    initScheduler(pipeline)  // 初始化Cron和Webhook路由
    c.JSON(200, gin.H{"pipeline_id": pipeline.ID})
}


3. 任务调度引擎

• Cron 调度（基于 robfig/cron/v3）
  func initCron(pipeline Pipeline) {
      c := cron.New()
      for _, trigger := range pipeline.Triggers {
          if trigger.Type == "cron" {
              c.AddFunc(trigger.Expression, func() {
                  queue.Push(pipeline) // 将任务推入队列
              })
          }
      }
      c.Start()
  }
  
• Webhook 触发
  func initWebhook(pipeline Pipeline) {
      for _, trigger := range pipeline.Triggers {
          if trigger.Type == "webhook" {
              ginRouter.POST(trigger.Endpoint, func(c *gin.Context) {
                  queue.Push(pipeline)
                  c.Status(202)
              })
          }
      }
  }
  

4. 任务执行器（Docker 隔离）

func TaskExecutor(workerID int, taskChan <-chan Task, resultChan chan<- TaskResult) {
    cli, _ := docker.NewClientWithOpts(docker.FromEnv)
    for task := range taskChan {
        // 创建 Docker 容器
        container, _ := cli.ContainerCreate(context.Background(), &container.Config{
            Image: task.Image,
            Cmd:   task.Command,
        }, nil, nil, nil, "")

        // 启动容器并等待完成
        cli.ContainerStart(context.Background(), container.ID, types.ContainerStartOptions{})
        statusCh, _ := cli.ContainerWait(context.Background(), container.ID, container.WaitConditionNotRunning)
        <-statusCh

        // 获取日志
        logs, _ := cli.ContainerLogs(context.Background(), container.ID, types.ContainerLogsOptions{ShowStdout: true, ShowStderr: true})
        logData, _ := ioutil.ReadAll(logs)

        // 返回结果
        resultChan <- TaskResult{
            TaskID:  task.ID,
            Status:  "completed",
            Logs:    string(logData),
        }
    }
}


5. 并发控制与状态同步

// 启动 Worker Pool（可配置并发数）
func StartWorkers(poolSize int) {
    taskChan := make(chan Task, 100)
    resultChan := make(chan TaskResult, 100)
    
    // 启动 Worker
    for i := 0; i < poolSize; i++ {
        go TaskExecutor(i, taskChan, resultChan)
    }
    
    // 状态更新协程
    go func() {
        for result := range resultChan {
            db.UpdateTaskStatus(result.TaskID, result.Status, result.Logs)
            notifyDependents(result.TaskID) // 通知依赖此任务的其他任务
        }
    }()
}

// 任务依赖解析（拓扑排序）
func resolveDependencies(tasks []TaskDef) []Task {
    dag := buildDependencyGraph(tasks) // 构建有向无环图
    return topologicalSort(dag)        // 返回可执行顺序
}


⚙️ 关键优化点

1. 高并发处理
   • Worker Pool 模式：固定数量的 Goroutine 复用，避免频繁创建销毁

   • 带缓冲 Channel：任务队列 (taskChan) 和结果队列 (resultChan) 缓冲防止阻塞

2. 依赖管理
   • 拓扑排序算法：确保任务按依赖顺序执行

   • 依赖完成广播：任务完成时触发依赖它的任务检查：
     func notifyDependents(taskID string) {
         for _, waitingTask := range dependencyMap[taskID] {
             atomic.AddInt(&waitingTask.DepCount, -1)
             if waitingTask.DepCount == 0 {
                 taskChan <- waitingTask // 放入执行队列
             }
         }
     }
     

3. 容器资源回收
   defer cli.ContainerRemove(context.Background(), container.ID, types.ContainerRemoveOptions{Force: true})
   

安全与稳定性

1. 超时控制
   ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
   defer cancel()
   statusCh, errCh := cli.ContainerWait(ctx, container.ID, container.WaitConditionNotRunning)
   select {
   case err := <-errCh:
       // 处理超时或错误
   case <-statusCh:
   }
   

2. 日志截断防溢出
   if len(logData) > 10*1024 { // 限制日志大小为10KB
       logData = append(logData[:10240], "...[TRUNCATED]"...)
   }
   

3. 资源限制
   HostConfig: &container.HostConfig{
       Resources: container.Resources{
           MemoryMB: 512, // 限制内存
           CPUQuota: 50000, // 限制CPU (50%)
       }
   }
   

数据库设计（简略）

CREATE TABLE pipelines (
    id VARCHAR(50) PRIMARY KEY,
    yaml_config TEXT NOT NULL
);

CREATE TABLE tasks (
    id VARCHAR(50) PRIMARY KEY,
    pipeline_id VARCHAR(50),
    status ENUM('pending', 'running', 'completed', 'failed'),
    stdout TEXT,
    stderr TEXT,
    FOREIGN KEY (pipeline_id) REFERENCES pipelines(id)
);


执行流程示例

1. 上传 YAML → Gin 解析并注册 Cron/Webhook
2. Cron 触发 → 任务入队 → Worker 从队列获取任务
3. Docker 执行 → 日志捕获 → 结果写入 Channel
4. 状态更新协程 → 更新数据库 → 触发依赖任务
5. Webhook 调用：POST /trigger/data_pipeline 手动触发流水线





---

## 1. 核心功能概述

| 功能 | 说明 |
|------|------|
| **YAML 文件解析** | 客户端上传 YAML 文件，定义任务依赖关系、触发方式和命令。 |
| **任务依赖管理** | 任务之间可设置依赖关系（DAG），确保依赖任务先执行。 |
| **触发方式支持** | 支持 Cron 定时触发和 Webhook 手动触发。 |
| **Docker 隔离执行** | 每个任务在独立 Docker 容器中执行，隔离执行环境。 |
| **并发控制** | 支持最大并发任务数限制，避免资源过载。 |
| **任务状态更新** | 通过 Channel 通信，任务完成后更新数据库状态。 |

---

## 2. 项目结构

```
.
├── main.go                    # 主程序入口
├── handler/                   # HTTP 请求处理逻辑
│   └── task_handler.go
├── scheduler/                 # 任务调度器（Cron/Webhook）
│   └── scheduler.go
├── executor/                  # 任务执行器（Docker）
│   └── docker_executor.go
├── model/                     # 数据结构定义
│   └── task_model.go
├── storage/                   # 数据库存储逻辑
│   └── db.go
└── utils/                     # 工具函数
    └── yaml_parser.go
```

---

## 3. YAML 文件格式示例

```yaml
tasks:
  - name: task1
    command: echo "Hello World"
    dependencies: []
    trigger:
      type: cron
      schedule: "*/1 * * * *"
    image: alpine:latest
  - name: task2
    command: sleep 5 && echo "Done"
    dependencies: [task1]
    trigger:
      type: webhook
    image: ubuntu:latest
```

---

## 4. 核心代码实现

### 4.1 任务模型定义（`model/task_model.go`）

```go
package model

type TaskDefinition struct {
	Name        string            `yaml:"name"`
	Command     string            `yaml:"command"`
	Dependencies []string        `yaml:"dependencies"`
	Trigger     TaskTrigger     `yaml:"trigger"`
	Image       string            `yaml:"image"` // Docker 镜像
}

type TaskTrigger struct {
	Type     string `yaml:"type"`     // cron 或 webhook
	Schedule string `yaml:"schedule"` // cron 表达式
}
```

---

### 4.2 YAML 解析（`utils/yaml_parser.go`）

```go
package utils

import (
	"io/ioutil"
	"gopkg.in/yaml.v2"
	"your_project/model"
)

func ParseYAMLFile(filePath string) ([]model.TaskDefinition, error) {
	data, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	var config struct {
		Tasks []model.TaskDefinition `yaml:"tasks"`
	}
	err = yaml.Unmarshal(data, &config)
	if err != nil {
		return nil, err
	}
	return config.Tasks, nil
}
```

---

### 4.3 Docker 执行器（`executor/docker_executor.go`）

```go
package executor

import (
	"context"
	"github.com/docker/docker/api/types"
	"github.com/docker/docker/client"
	"io"
	"log"
	"time"
)

type DockerExecutor struct {
	Client *client.Client
}

func NewDockerExecutor() (*DockerExecutor, error) {
	cli, err := client.NewClientWithOpts(client.FromEnv, client.WithAPIVersionNegotiation())
	if err != nil {
		return nil, err
	}
	return &DockerExecutor{Client: cli}, nil
}

func (e *DockerExecutor) RunTask(task model.TaskDefinition, outputChan chan<- TaskResult) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	resp, err := e.Client.ContainerCreate(
		ctx,
		&container.Config{
			Image: task.Image,
			Cmd:   []string{"sh", "-c", task.Command},
		},
		nil, nil, nil, "",
	)
	if err != nil {
		outputChan <- TaskResult{Error: err.Error()}
		return
	}

	if err := e.Client.ContainerStart(ctx, resp.ID, types.ContainerStartOptions{}); err != nil {
		outputChan <- TaskResult{Error: err.Error()}
		return
	}

	statusCh, errCh := e.Client.ContainerWait(ctx, resp.ID, container.WaitConditionNotRunning)
	select {
	case err := <-errCh:
		if err != nil {
			outputChan <- TaskResult{Error: err.Error()}
		}
	case status := <-statusCh:
		if status.StatusCode != 0 {
			outputChan <- TaskResult{Error: "non-zero exit code"}
		}
	}

	out, err := e.Client.ContainerLogs(ctx, resp.ID, types.ContainerLogsOptions{ShowStdout: true, ShowStderr: true})
	if err != nil {
		outputChan <- TaskResult{Error: err.Error()}
		return
	}

	stdout, _ := io.ReadAll(out.Stdout)
	stderr, _ := io.ReadAll(out.Stderr)

	outputChan <- TaskResult{
		Success: true,
		Stdout:  string(stdout),
		Stderr:  string(stderr),
	}
}
```

---

### 4.4 任务调度器（`scheduler/scheduler.go`）

```go
package scheduler

import (
	"github.com/robfig/cron/v3"
	"your_project/model"
	"your_project/executor"
	"your_project/storage"
	"sync"
)

type Scheduler struct {
	TaskMap    map[string]model.TaskDefinition
	Executor   *executor.DockerExecutor
	DB         *storage.Database
	MaxWorkers int
	JobChan    chan model.TaskDefinition
	ResultsChan chan executor.TaskResult
	Mutex      sync.Mutex
}

func NewScheduler(maxWorkers int) *Scheduler {
	return &Scheduler{
		TaskMap:    make(map[string]model.TaskDefinition),
		Executor:   executor.NewDockerExecutor(),
		DB:         storage.NewDB(),
		MaxWorkers: maxWorkers,
		JobChan:    make(chan model.TaskDefinition, maxWorkers),
		ResultsChan: make(chan executor.TaskResult),
	}
}

func (s *Scheduler) RegisterTask(task model.TaskDefinition) {
	s.Mutex.Lock()
	s.TaskMap[task.Name] = task
	s.Mutex.Unlock()

	if task.Trigger.Type == "cron" {
		c := cron.New()
		_, _ = c.AddFunc(task.Trigger.Schedule, func() {
			s.JobChan <- task
		})
		c.Start()
	}
}

func (s *Scheduler) StartWorkers() {
	for i := 0; i < s.MaxWorkers; i++ {
		go func() {
			for task := range s.JobChan {
				s.Executor.RunTask(task, s.ResultsChan)
			}
		}()
	}
}

func (s *Scheduler) MonitorResults() {
	for result := range s.ResultsChan {
		s.DB.UpdateTaskStatus(result.TaskID, result.Success, result.Stdout, result.Stderr)
	}
}
```

---

### 4.5 Gin 服务器（`main.go`）

```go
package main

import (
	"github.com/gin-gonic/gin"
	"your_project/handler"
	"your_project/scheduler"
)

func main() {
	r := gin.Default()

	scheduler := scheduler.NewScheduler(5) // 最大并发 5 个任务

	// 接收 YAML 文件
	r.POST("/upload", handler.UploadYAMLHandler(scheduler))

	// Webhook 触发任务
	r.POST("/trigger/:taskName", handler.TriggerTaskHandler(scheduler))

	// 启动工作线程和结果监听
	scheduler.StartWorkers()
	go scheduler.MonitorResults()

	r.Run(":8080")
}
```

---

## 5. 测试流程

1. **上传 YAML 文件**：
   ```bash
   curl -X POST http://localhost:8080/upload -F "file=@tasks.yaml"
   ```

2. **Webhook 触发任务**：
   ```bash
   curl -X POST http://localhost:8080/trigger/task2
   ```

3. **观察任务执行**：
   - 检查数据库中的任务状态是否更新。
   - 查看 Docker 容器日志输出。

---

## 6. 注意事项

- **Docker 安全性**：确保只允许可信的镜像执行，避免命令注入攻击。
- **资源隔离**：设置 Docker 容器的资源限制（CPU/Memory）。
- **并发控制**：使用 `MaxWorkers` 控制最大并发任务数，避免资源耗尽。
- **依赖管理**：需实现 DAG 拓扑排序，避免任务依赖冲突。
- **持久化存储**：任务状态应持久化到数据库（如 MySQL/PostgreSQL）。

---

通过以上架构，你可以实现一个灵活、安全、可扩展的任务调度系统，支持多种触发方式、依赖管理和并发执行。