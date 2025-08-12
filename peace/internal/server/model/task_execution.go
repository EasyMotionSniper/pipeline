package model

import "gorm.io/gorm"

type TaskExecution struct {
	gorm.Model
	PipelineExecuteUUID string `gorm:"not null;type:varchar(50);uniqueIndex:idx_execution_uuid_task_name"`
	TaskName            string `gorm:"type:varchar(50);not null;uniqueIndex:idx_execution_uuid_task_name"`
	Status              string `gorm:"type:ENUM('pending', 'running', 'success', 'failed', 'skipped');not null"`
	Stdout              string `gorm:"type:text"`
	Stderr              string `gorm:"type:text"`
}
