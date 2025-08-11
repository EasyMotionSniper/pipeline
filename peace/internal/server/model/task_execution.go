package model

import "gorm.io/gorm"

type TaskExecution struct {
	gorm.Model
	PipelineExecutionUUID uint64 `gorm:"not null;index"`
	TaskName              string `gorm:"type:varchar(100);not null"`
	Command               string `gorm:"type:text;not null"`
	ResultCheck           string `gorm:"type:text"`
	Status                string `gorm:"type:ENUM('pending', 'running', 'success', 'failed', 'skipped');not null"`
	Stdout                string `gorm:"type:text"`
	Stderr                string `gorm:"type:text"`
}
