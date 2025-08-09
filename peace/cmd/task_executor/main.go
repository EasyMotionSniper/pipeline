package main

import (
	"log"
	"pace/internal/task_executor/docker"
)

func main() {
	d := docker.NewDockerClient()
	stdout, stderr, err := d.RunTask("echo aa && ls /aaa")
	if err != nil {
		log.Fatalf("Failed to run task: %v", err)
	}
	log.Printf("stdout: %s", stdout)
	log.Printf("stderr: %s", stderr)
}
