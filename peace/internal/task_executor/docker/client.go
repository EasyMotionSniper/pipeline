package docker

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"time"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/client"
	"github.com/docker/docker/pkg/stdcopy"
)

type DockerClient struct {
	cli *client.Client
}

func NewDockerClient() *DockerClient {
	cli, err := client.NewClientWithOpts(
		client.WithHost("unix:///var/run/docker.sock"),
		client.WithAPIVersionNegotiation(),
	)
	if err != nil {
		log.Fatalf("Failed to connect to Docker: %v", err)
	}
	return &DockerClient{cli}
}

func (d *DockerClient) RunTask(command string) (string, string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	resp, err := d.cli.ContainerCreate(
		ctx,
		&container.Config{
			Image: "docker.1ms.run/alpine",
			Cmd:   []string{"sh", "-c", command},
		},
		&container.HostConfig{
			AutoRemove: false, // 禁用自动删除，避免日志获取失败
		},
		nil, nil, "",
	)
	if err != nil {
		return "", "", err
	}
	containerID := resp.ID // 保存容器ID，后续删除用

	// 启动容器
	if err := d.cli.ContainerStart(ctx, containerID, container.StartOptions{}); err != nil {
		return "", "", err
	}
	fmt.Println("container id:", containerID)

	// 等待容器执行完成
	statusCh, errCh := d.cli.ContainerWait(ctx, containerID, container.WaitConditionNotRunning)
	select {
	case err := <-errCh:
		if err != nil {
			return "", "", err
		}
	case <-statusCh:
	}

	// 获取日志（此时容器未被删除，可正常获取）
	out, err := d.cli.ContainerLogs(ctx, containerID, container.LogsOptions{
		ShowStdout: true,
		ShowStderr: true,
	})
	if err != nil {
		// 即使获取日志失败，也尝试删除容器
		d.cli.ContainerRemove(ctx, containerID, container.RemoveOptions{Force: true})
		return "", "", err
	}

	// 分离stdout和stderr
	stdout, stderr := new(bytes.Buffer), new(bytes.Buffer)
	_, _ = stdcopy.StdCopy(stdout, stderr, out)

	// 手动删除容器（在日志获取完成后）
	if err := d.cli.ContainerRemove(ctx, containerID, container.RemoveOptions{Force: true}); err != nil {
		log.Printf("Warning: 容器删除失败: %v", err) // 只警告，不阻断主流程
	}

	return stdout.String(), stderr.String(), nil
}
