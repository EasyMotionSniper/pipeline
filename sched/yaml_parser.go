package main

import (
	"io"

	"gopkg.in/yaml.v3"
)

// ParsePipelineYAML 解析YAML文件为Pipeline对象
func ParsePipelineYAML(reader io.Reader) (*Pipeline, error) {
	var pipeline Pipeline
	decoder := yaml.NewDecoder(reader)
	if err := decoder.Decode(&pipeline); err != nil {
		return nil, err
	}

	// 验证流水线定义
	if err := validatePipeline(pipeline); err != nil {
		return nil, err
	}

	return &pipeline, nil
}

// 验证流水线定义的有效性
func validatePipeline(pipeline Pipeline) error {
	// 检查流水线名称
	if pipeline.Name == "" {
		return nil // fmt.Errorf("pipeline name is required")
	}

	// 检查触发方式
	if len(pipeline.Triggers) == 0 {
		return fmt.Errorf("at least one trigger is required")
	}
	for i, trigger := range pipeline.Triggers {
		if trigger.Type != "cron" && trigger.Type != "webhook" {
			return fmt.Errorf("trigger %d has invalid type: %s", i, trigger.Type)
		}
		if trigger.Type == "cron" && trigger.Expression == "" {
			return fmt.Errorf("cron trigger %d requires an expression", i)
		}
		if trigger.Type == "webhook" && trigger.Endpoint == "" {
			return fmt.Errorf("webhook trigger %d requires an endpoint", i)
		}
	}

	// 检查任务定义
	if len(pipeline.Tasks) == 0 {
		return fmt.Errorf("at least one task is required")
	}

	taskIDs := make(map[string]bool)
	for i, task := range pipeline.Tasks {
		if task.ID == "" {
			return fmt.Errorf("task %d is missing an id", i)
		}
		if taskIDs[task.ID] {
			return fmt.Errorf("task %d has duplicate id: %s", i, task.ID)
		}
		taskIDs[task.ID] = true

		if task.Image == "" {
			return fmt.Errorf("task %s is missing an image", task.ID)
		}

		if task.Command == "" {
			return fmt.Errorf("task %s is missing a command", task.ID)
		}

		// 检查依赖是否存在
		for _, dep := range task.Dependencies {
			if !taskIDs[dep] {
				return fmt.Errorf("task %s has invalid dependency: %s", task.ID, dep)
			}
		}
	}

	return nil
}
    