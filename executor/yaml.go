package executor

import (
	"io/ioutil"

	"yourproject/models"

	"gopkg.in/yaml.v3"
)

// YAML结构定义，用于解析输入的YAML文件
type YAMLConfig struct {
	Name        string        `yaml:"name"`
	Description string        `yaml:"description"`
	Triggers    []YAMLTrigger `yaml:"triggers"`
	Tasks       []YAMLTask    `yaml:"tasks"`
}

type YAMLTrigger struct {
	Cron    string `yaml:"cron,omitempty"`
	Webhook string `yaml:"webhook,omitempty"`
}

type YAMLTask struct {
	Name             string   `yaml:"name"`
	Command          string   `yaml:"command"`
	ResultCheck      string   `yaml:"result_check,omitempty"`
	DependsOnSuccess []string `yaml:"depends_on_success,omitempty"`
	DependsOnFailure []string `yaml:"depends_on_failure,omitempty"`
}

// ParseYAMLFile 从文件解析YAML配置并转换为Pipeline模型
func ParseYAMLFile(path string) (*models.Pipeline, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	return ParseYAML(data)
}

// ParseYAML 从字节数据解析YAML配置并转换为Pipeline模型
func ParseYAML(data []byte) (*models.Pipeline, error) {
	var config YAMLConfig
	if err := yaml.Unmarshal(data, &config); err != nil {
		return nil, err
	}

	pipeline := &models.Pipeline{
		Name:        config.Name,
		Description: config.Description,
	}

	// 转换triggers
	for _, t := range config.Triggers {
		pipeline.Triggers = append(pipeline.Triggers, models.Trigger{
			Cron:    t.Cron,
			Webhook: t.Webhook,
		})
	}

	// 转换tasks
	for _, task := range config.Tasks {
		pipeline.Tasks = append(pipeline.Tasks, models.Task{
			Name:             task.Name,
			Command:          task.Command,
			ResultCheck:      task.ResultCheck,
			DependsOnSuccess: task.DependsOnSuccess,
			DependsOnFailure: task.DependsOnFailure,
		})
	}

	return pipeline, nil
}
