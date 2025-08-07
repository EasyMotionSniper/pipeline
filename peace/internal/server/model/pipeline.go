package model

import (
	"gopkg.in/yaml.v3"
	"gorm.io/gorm"
)

type Pipeline struct {
	gorm.Model
	Name        string `gorm:"varchar(255);not null;uniqueIndex:idx_name_version"`
	Description string `gorm:"type:text"`
	Version     int    `gorm:"not null;uniqueIndex:idx_name_version"`
	Config      string `gorm:"type:text;not null"`
}

type PipelineConfig struct {
	Name        string    `yaml:"name"`
	Description string    `yaml:"description"`
	Triggers    []Trigger `yaml:"triggers"`
	Tasks       []Task    `yaml:"tasks"`
}

type Trigger struct {
	Cron    string `yaml:"cron,omitempty"`
	Webhook string `yaml:"webhook,omitempty"`
}

type Task struct {
	Name             string   `yaml:"name"`
	Command          string   `yaml:"command"`
	ResultCheck      string   `yaml:"result_check,omitempty"`
	DependsOnSuccess []string `yaml:"depends_on_success,omitempty"` // 依赖成功的任务
	DependsOnFailure []string `yaml:"depends_on_failure,omitempty"` // 依赖失败的任务
}

func ParsePipelineConfig(yamlContent string) (*PipelineConfig, error) {
	var config PipelineConfig
	if err := yaml.Unmarshal([]byte(yamlContent), &config); err != nil {
		return nil, err
	}
	return &config, nil
}
