package main

import (
	rpcserver "pace/internal/task_executor/rpc_server"
)

func main() {
	client, err := rpcserver.NewServer(":8082")
	if err != nil {
		panic(err)
	}
	client.Start(":8081")
}
