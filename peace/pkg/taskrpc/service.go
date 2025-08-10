package taskrpc

// TaskExecutorService 定义 Executor 需实现的 RPC 服务接口
type TaskExecutorService interface {
	// ExecuteTask 执行任务（非阻塞：立即返回，任务异步执行）
	ExecuteTask(req *ExecutePipelineRequest, resp *ExecutePipelineResponse) error
}

// BackendCallbackService 定义后端需实现的回调接口
// Executor 通过这些方法向后端推送状态
type BackendCallbackService interface {
	PushStatus(status *TaskStatusUpdateRequest, resp *TaskStatusUpdateResponse) error
}
