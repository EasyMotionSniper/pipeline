package model

import (
	"time"

	"gorm.io/gorm"
)

type PipelineExecution struct {
	gorm.Model
	PipelineExecuteUUID string `gorm:"not null;type:varchar(50);uniqueIndex"`
	PipelineVersionID   uint   `gorm:"not null"`
	TriggerType         string `gorm:"type:ENUM('manual', 'cron', 'webhook');not null"`
	Status              string `gorm:"type:ENUM('running', 'success', 'failed')"`
	// created_at is automatically set by GORM
	// but we define it to create an index for performance
	CreatedAt time.Time `gorm:"index"`
}
