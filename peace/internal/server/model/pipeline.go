package model

import (
	"pace/pkg/taskrpc"

	"gopkg.in/yaml.v3"
	"gorm.io/gorm"
)

type Pipeline struct {
	gorm.Model
	Name        string `gorm:"type:varchar(100);not null;uniqueIndex:idx_name_version"`
	Description string `gorm:"type:text"`
	Version     int    `gorm:"not null;uniqueIndex:idx_name_version"`
	Config      string `gorm:"type:text;not null"`
}

type PipelineConfig struct {
	Name        string         `yaml:"name"`
	Description string         `yaml:"description"`
	Triggers    []Trigger      `yaml:"triggers"`
	Tasks       []taskrpc.Task `yaml:"tasks"`
}

type Trigger struct {
	Cron    string `yaml:"cron,omitempty"`
	Webhook string `yaml:"webhook,omitempty"`
}

func ParsePipelineConfig(yamlContent string) (*PipelineConfig, error) {
	var config PipelineConfig
	if err := yaml.Unmarshal([]byte(yamlContent), &config); err != nil {
		return nil, err
	}
	return &config, nil
}
