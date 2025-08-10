package taskrpc

type Task struct {
	Name             string   `json:"name"`
	Command          string   `json:"command"`
	ResultCheck      string   `json:"result_check,omitempty"`
	DependsOnSuccess []string `json:"depends_on_success,omitempty"`
	DependsOnFailure []string `json:"depends_on_failure,omitempty"`
}

type ExecutePipelineRequest struct {
	ExecutionID int    `json:"execution_id"`
	Tasks       []Task `json:"tasks"`
}

type ExecutePipelineResponse struct {
}

type TaskStatusUpdate struct {
	ExecutionID int    `json:"execution_id"`
	TaskName    string `json:"task_name"`
	Status      string `json:"status"` // pending/running/success/failed
	Stdout      string `json:"stdout"`
	Stderr      string `json:"stderr"`
}

type TaskStatusUpdateRequest struct {
	TaskStatusUpdate TaskStatusUpdate `json:"task_status_update"`
}

type TaskStatusUpdateResponse struct {
}
