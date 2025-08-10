package rpccall

import (
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
	"pace/pkg/taskrpc"
)

type Client struct {
	rpcClient *rpc.Client
}

func NewClient(executorRPCAddr string) (*Client, error) {
	conn, err := net.Dial("tcp", executorRPCAddr)
	if err != nil {
		return nil, err
	}
	return &Client{
		rpcClient: jsonrpc.NewClient(conn),
	}, nil
}

func (c *Client) ExecuteTask(req *taskrpc.ExecutePipelineRequest) (*taskrpc.ExecutePipelineResponse, error) {
	var resp taskrpc.ExecutePipelineResponse
	err := c.rpcClient.Call("TaskExecutorService.ExecutePipeline", req, &resp)
	return &resp, err
}
