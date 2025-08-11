package queue

import "gopkg.in/yaml.v3"

const PIPELINE_EXECUTE = "pipeline:execute"
const PIPELINE_STATUS_UPDATE = "pipeline:status:update"
const TASK_STATUS_UPDATE = "task:status:update"

type Trigger struct {
	Cron    string `yaml:"cron,omitempty"`
	Webhook string `yaml:"webhook,omitempty"`
}

type Task struct {
	Name        string   `json:"name"`
	Command     string   `json:"command"`
	ResultCheck string   `json:"result_check,omitempty"`
	DependsOn   []string `json:"depends_on,omitempty"`
}

type PipelineConfig struct {
	Name        string    `yaml:"name"`
	Description string    `yaml:"description"`
	Triggers    []Trigger `yaml:"triggers"`
	Tasks       []Task    `yaml:"tasks"`
}

type PipelineExecuteInfo struct {
	PipelineVersionID uint   `json:"pipeline_version_id"`
	Tasks             []Task `json:"tasks"`
}

type PipelineStatusUpdate struct {
	PipelineVersionID   uint   `json:"pipeline_version_id"`
	PipelineExecuteUUID string `json:"pipeline_execute_uuid"`
	Status              string `json:"status"` // running/success/failed
}

type TaskStatusUpdate struct {
	PipelineExecuteUUID string `json:"pipeline_execute_uuid"`
	TaskName            string `json:"task_name"`
	Status              string `json:"status"` // pending/running/success/failed/skipped
	Stdout              string `json:"stdout"`
	Stderr              string `json:"stderr"`
}

func ParsePipelineConfig(yamlContent string) (*PipelineConfig, error) {
	var config PipelineConfig
	if err := yaml.Unmarshal([]byte(yamlContent), &config); err != nil {
		return nil, err
	}
	return &config, nil
}
