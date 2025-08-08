package model

import (
	"gorm.io/gorm"
)

type PipelineExecution struct {
	gorm.Model
	PipelineID  uint   `gorm:"not null;index"`
	TriggerType string `gorm:"type:ENUM('manual', 'crontab', 'webhook');not null"`
	Status      string `gorm:"type:ENUM('running', 'success', 'failed')"`
}
