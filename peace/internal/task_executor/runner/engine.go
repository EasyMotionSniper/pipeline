package runner

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"log"
	"pace/pkg/queue"
	"time"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/client"
	"github.com/docker/docker/pkg/stdcopy"
)

type DockerEngine struct {
	cli *client.Client
}

func NewDockerEngine() (*DockerEngine, error) {
	cli, err := client.NewClientWithOpts(
		client.WithHost("unix:///var/run/docker.sock"),
		client.WithAPIVersionNegotiation(),
	)
	if err != nil {
		log.Fatalf("Failed to connect to Docker: %v", err)
		return nil, err
	}
	return &DockerEngine{cli}, nil
}

func (d *DockerEngine) ExecuteTask(task queue.Task) (queue.TaskStatusUpdate, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	taskStatus := queue.TaskStatusUpdate{}
	resp, err := d.cli.ContainerCreate(
		ctx,
		&container.Config{
			Image: "docker.1ms.run/alpine:3.17",
			Cmd:   []string{"sh", "-c", task.Command},
		},
		nil, nil, nil, "",
	)
	if err != nil {
		return taskStatus, err
	}
	containerID := resp.ID

	defer func() {
		err := d.cli.ContainerRemove(ctx, containerID, container.RemoveOptions{Force: true})
		if err != nil {
			log.Printf("fail to remove container %s: %v", containerID, err)
		}
	}()

	if err := d.cli.ContainerStart(ctx, containerID, container.StartOptions{}); err != nil {
		return taskStatus, errors.New("fail to start container")
	}

	statusCh, errCh := d.cli.ContainerWait(ctx, containerID, container.WaitConditionNotRunning)
	select {
	case err := <-errCh:
		if err != nil {
			return taskStatus, err
		}
	case status := <-statusCh:
		log.Printf("container %s exit status: %d", containerID, status.StatusCode)
		if status.StatusCode != 0 {
			taskStatus.Status = "failed"
		} else {
			taskStatus.Status = "success"
		}
	}

	out, err := d.cli.ContainerLogs(ctx, containerID, container.LogsOptions{
		ShowStdout: true,
		ShowStderr: true,
	})

	if err != nil {
		return taskStatus, fmt.Errorf("fail to get container logs: %v", err)
	}

	stdout, stderr := new(bytes.Buffer), new(bytes.Buffer)
	_, err = stdcopy.StdCopy(stdout, stderr, out)
	if err != nil {
		return taskStatus, fmt.Errorf("fail to copy container logs: %v", err)
	}
	taskStatus.Stdout = stdout.String()
	taskStatus.Stderr = stderr.String()

	return taskStatus, nil
}
