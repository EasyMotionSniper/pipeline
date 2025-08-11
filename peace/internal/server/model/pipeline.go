package model

import (
	"gorm.io/gorm"
)

type Pipeline struct {
	gorm.Model
	Name          string `gorm:"type:varchar(128);uniqueIndex" json:"name"` // 唯一名称
	Description   string `gorm:"type:text" json:"description"`
	LatestVersion int    `gorm:"default:0" json:"latest_version"` // 最新版本号
}

// PipelineVersion 存储每个版本的配置（关联到Pipeline）
type PipelineVersion struct {
	gorm.Model
	PipelineID uint   `gorm:"uniqueIndex:idx_pipeline_version" json:"pipeline_id"`      // 关联Pipeline.ID
	Version    int    `gorm:"type:int;uniqueIndex:idx_pipeline_version" json:"version"` // 版本号（每个pipeline独立自增）
	Config     string `gorm:"type:text" json:"config"`                                  // 完整配置（YAML字符串）
}
