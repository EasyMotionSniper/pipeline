package rpccall

import (
	"fmt"
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
	"pace/pkg/taskrpc"
)

// CallbackServer 实现 rpc.BackendCallbackService 接口
type CallbackServer struct {
}

// PushStatus 接收 Executor 推送的状态更新
func (s *CallbackServer) PushStatus(status *taskrpc.TaskStatusUpdateRequest, resp *taskrpc.TaskStatusUpdateResponse) error {
	// 处理状态更新（如存入数据库）
	fmt.Printf("Received status update: %+v\n", status.TaskStatusUpdate)
	return nil
}

func (s *CallbackServer) Start(addr string) error {
	rpc.RegisterName("BackendCallbackService", s)
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
