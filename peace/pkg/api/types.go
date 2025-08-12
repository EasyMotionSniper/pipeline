package api

type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type TriggerRequest struct {
	PipelineName string `json:"pipeline_name"`
}
