// pkg/api/pipeline.go
package api

type PipelineBrief struct {
	ID          uint   `json:"id"`
	Name        string `json:"name"`
	Description string `json:"description"`
}
type PipelineDetail struct {
	Config string `json:"config"` // 最新版本的完整配置（JSON字符串）
}

type ExecutionHistoryBrief struct {
	ID          uint   `json:"id"`
	PipelineID  uint   `json:"pipeline_id"`        // 关联的Pipeline ID
	Status      string `json:"status"`             // 执行状态（如：running, success,
	TriggerType string `json:"trigger_type"`       // 触发类型（如：manual, cron, webhook）
	StartTime   string `json:"start_time"`         // 执行开始时间
	EndTime     string `json:"end_time,omitempty"` // 执行结束时间
}

type TaskDetail struct {
	TaskName    string `json:"task_name"`
	Command     string `json:"command"`
	ResultCheck string `json:"result_check"`
	Status      string `json:"status"`
	Stdout      string `json:"stdout"`
	Stderr      string `json:"stderr"`
	StartTime   string `json:"start_time"`         // 执行开始时间
	EndTime     string `json:"end_time,omitempty"` // 执行结束时间
}

type ExecutionHistoryDetail struct {
	Config string       `json:"config"`
	Tasks  []TaskDetail `json:"tasks"`
}
