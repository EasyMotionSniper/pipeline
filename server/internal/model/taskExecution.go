package model

import "gorm.io/gorm"

type TaskExecution struct {
	gorm.Model
	ExecutionID uint64 `gorm:"not null;index"` // 关联 PipelineExecution
	TaskName    string `gorm:"type:varchar(100);not null"`
	Command     string `gorm:"type:text;not null"`
	Status      string `gorm:"type:ENUM('pending', 'running', 'success', 'failed')"`
	Stdout      string `gorm:"type:text"`
	Stderr      string `gorm:"type:text"`
}
