package api

type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type TriggerRequest struct {
	PipelineID int `json:"pipeline_id"`
}

type TriggerResponse struct {
	ExecutionID int `json:"execution_id"`
}
