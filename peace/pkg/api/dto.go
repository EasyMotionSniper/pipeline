// pkg/api/pipeline.go
package api

type PipelineBrief struct {
	Name        string `json:"name"`
	Description string `json:"description"`
}
type PipelineDetail struct {
	Name        string `json:"name"`        // 名称
	Description string `json:"description"` // 描述
	Config      string `json:"config"`      // 最新版本的完整配置（JSON字符串）
}
