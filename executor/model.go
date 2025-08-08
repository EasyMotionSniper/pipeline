package executor

import (
	"time"

	"gorm.io/gorm"
)

// Pipeline 表示一个完整的任务流水线
type Pipeline struct {
	ID          uint           `gorm:"primaryKey" json:"id"`
	Name        string         `gorm:"uniqueIndex" json:"name"`
	Description string         `json:"description"`
	Version     int            `json:"version"` // 版本号，每次更新递增
	Triggers    []Trigger      `gorm:"foreignKey:PipelineID" json:"triggers"`
	Tasks       []Task         `gorm:"foreignKey:PipelineID" json:"tasks"`
	CreatedAt   time.Time      `json:"created_at"`
	UpdatedAt   time.Time      `json:"updated_at"`
	DeletedAt   gorm.DeletedAt `gorm:"index" json:"-"`
}

// Trigger 表示流水线的触发方式
type Trigger struct {
	ID         uint           `gorm:"primaryKey" json:"id"`
	PipelineID uint           `json:"pipeline_id"`
	Cron       string         `json:"cron,omitempty"`    // cron表达式，为空则不使用
	Webhook    string         `json:"webhook,omitempty"` // webhook路径，为空则不使用
	CreatedAt  time.Time      `json:"created_at"`
	UpdatedAt  time.Time      `json:"updated_at"`
	DeletedAt  gorm.DeletedAt `gorm:"index" json:"-"`
}

// Task 表示流水线中的一个任务
type Task struct {
	ID               uint           `gorm:"primaryKey" json:"id"`
	PipelineID       uint           `json:"pipeline_id"`
	Name             string         `json:"name"`
	Command          string         `json:"command"`
	ResultCheck      string         `json:"result_check,omitempty"`                // 结果检查命令，可为空
	DependsOnSuccess []string       `gorm:"type:text[]" json:"depends_on_success"` // 依赖成功的任务名列表
	DependsOnFailure []string       `gorm:"type:text[]" json:"depends_on_failure"` // 依赖失败的任务名列表
	CreatedAt        time.Time      `json:"created_at"`
	UpdatedAt        time.Time      `json:"updated_at"`
	DeletedAt        gorm.DeletedAt `gorm:"index" json:"-"`
}

// PipelineExecution 记录流水线的一次执行
type PipelineExecution struct {
	ID          uint      `gorm:"primaryKey" json:"id"`
	PipelineID  uint      `json:"pipeline_id"`
	PipelineVer int       `json:"pipeline_ver"` // 执行时使用的流水线版本
	TriggerType string    `json:"trigger_type"` // cron, webhook, manual
	Status      string    `json:"status"`       // pending, running, completed, failed
	StartTime   time.Time `json:"start_time"`
	EndTime     time.Time `json:"end_time,omitempty"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`
}

// TaskExecution 记录任务的一次执行
type TaskExecution struct {
	ID             uint           `gorm:"primaryKey" json:"id"`
	PipelineExecID uint           `json:"pipeline_exec_id"`
	TaskID         uint           `json:"task_id"`
	TaskName       string         `json:"task_name"`
	Status         string         `json:"status"` // pending, running, success, failed
	StartTime      time.Time      `json:"start_time"`
	EndTime        time.Time      `json:"end_time,omitempty"`
	Stdout         string         `json:"stdout,omitempty"`
	Stderr         string         `json:"stderr,omitempty"`
	CreatedAt      time.Time      `json:"created_at"`
	UpdatedAt      time.Time      `json:"updated_at"`
	DeletedAt      gorm.DeletedAt `gorm:"index" json:"-"`
}
