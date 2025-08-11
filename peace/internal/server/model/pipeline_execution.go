package model

import (
	"gorm.io/gorm"
)

type PipelineExecution struct {
	gorm.Model
	PipelineVersionID   uint   `gorm:"not null;index"`
	PipelineExecuteUUID string `gorm:"not null;type:varchar(50);uniqueIndex"`
	TriggerType         string `gorm:"type:ENUM('manual', 'crontab', 'webhook');not null"`
	Status              string `gorm:"type:ENUM('running', 'success', 'failed')"`
}
