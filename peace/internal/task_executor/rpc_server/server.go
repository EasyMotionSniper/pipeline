package rpcserver

import (
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
	"pace/internal/task_executor/scheduler"
	"pace/pkg/taskrpc"
)

// Server 实现 rpc.TaskExecutorService 接口
type Server struct {
	sched *scheduler.TaskScheduler
}

func NewServer(backendRPCAddr string) (*Server, error) {
	// 连接后端的回调服务（用于推送状态）
	// conn, err := net.Dial("tcp", backendRPCAddr)
	// if err != nil {
	// 	return nil, err
	// }
	// callbackCli := jsonrpc.NewClient(conn)

	callback := func(status *taskrpc.TaskStatusUpdate) error {
		return nil
		/*
			var resp taskrpc.TaskStatusUpdateResponse
			return callbackCli.Call("BackendCallbackService.PushStatus", &taskrpc.TaskStatusUpdateRequest{
				TaskStatusUpdate: *status,
			}, &resp)
		*/
	}
	sched, err := scheduler.NewTaskScheduler(5, callback)
	if err != nil {
		return nil, err
	}
	return &Server{
		sched: sched,
	}, nil
}

// ExecuteTask 实现 RPC 方法：接收后端的任务执行请求（非阻塞）
func (s *Server) ExecuteTask(req *taskrpc.ExecutePipelineRequest, resp *taskrpc.ExecutePipelineResponse) error {
	go func() {
		s.sched.SchedulePipeline(req)
	}()

	return nil
}

func (s *Server) Start(addr string) error {
	if err := rpc.RegisterName("TaskExecutorService", s); err != nil {
		return err
	}

	listener, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	defer listener.Close()

	for {
		conn, err := listener.Accept()
		if err != nil {
			continue
		}
		go jsonrpc.ServeConn(conn)
	}
}
