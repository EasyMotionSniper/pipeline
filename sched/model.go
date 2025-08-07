package main

import (
	"time"
)

// Trigger 定义任务触发方式
type Trigger struct {
	Type       string `yaml:"type"`       // cron 或 webhook
	Expression string `yaml:"expression"` // cron表达式，仅用于cron类型
	Endpoint   string `yaml:"endpoint"`   // webhook端点，仅用于webhook类型
}

// Task 定义单个任务
type Task struct {
	ID           string   `yaml:"id"`
	Name         string   `yaml:"name"`
	Image        string   `yaml:"image"` // Docker镜像
	Command      string   `yaml:"command"`
	Dependencies []string `yaml:"dependencies,omitempty"` // 依赖的任务ID列表
	PipelineID   string   `json:"pipeline_id"`            // 所属流水线ID，非YAML字段
}

// Pipeline 定义流水线
type Pipeline struct {
	ID       string    `yaml:"id,omitempty"`
	Name     string    `yaml:"name"`
	Triggers []Trigger `yaml:"triggers"`
	Tasks    []Task    `yaml:"tasks"`
}

// TaskStatus 任务状态
type TaskStatus string

const (
	TaskStatusPending  TaskStatus = "pending"
	TaskStatusRunning  TaskStatus = "running"
	TaskStatusSuccess  TaskStatus = "success"
	TaskStatusFailed   TaskStatus = "failed"
	TaskStatusCanceled TaskStatus = "canceled"
)

// TaskStatusUpdate 任务状态更新
type TaskStatusUpdate struct {
	TaskID     string     `json:"task_id"`
	PipelineID string     `json:"pipeline_id"`
	Status     TaskStatus `json:"status"`
	Stdout     string     `json:"stdout,omitempty"`
	Stderr     string     `json:"stderr,omitempty"`
	CompletedAt time.Time `json:"completed_at,omitempty"`
}

// PipelineStatus 流水线状态
type PipelineStatus string

const (
	PipelineStatusCreated  PipelineStatus = "created"
	PipelineStatusRunning  PipelineStatus = "running"
	PipelineStatusCompleted PipelineStatus = "completed"
	PipelineStatusFailed   PipelineStatus = "failed"
)
    