package test

import (
	"testing"

	"pace/internal/task_executor/runner"
	"pace/pkg/queue"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestExecuteTask_OnlyCommandSuccess(t *testing.T) {
	engine, err := runner.NewDockerEngine()
	require.NoError(t, err)

	task := queue.Task{
		Name:    "test-success-only-command",
		Command: "echo 'hello world'",
	}

	status, err := engine.ExecuteTask(task)

	require.NoError(t, err)
	assert.Equal(t, "success", status.Status)
	assert.Contains(t, status.Stdout, "hello world")
	assert.Empty(t, status.Stderr)
}

func TestExecuteTask_CommandAndCheckSuccess(t *testing.T) {
	engine, err := runner.NewDockerEngine()
	require.NoError(t, err)

	task := queue.Task{
		Name:        "test-command-check-success",
		Command:     "echo 'test' > /tmp/check.txt",
		ResultCheck: "grep 'test' /tmp/check.txt",
	}

	status, err := engine.ExecuteTask(task)

	require.NoError(t, err)
	assert.Equal(t, "success", status.Status)
	assert.Empty(t, status.Stderr)
}

func TestExecuteTask_CommandSuccessCheckFailed(t *testing.T) {
	engine, err := runner.NewDockerEngine()
	require.NoError(t, err)

	task := queue.Task{
		Name:        "test-command-success-check-fail",
		Command:     "echo 'hello'",
		ResultCheck: "grep 'world' <<< 'hello'",
	}

	status, err := engine.ExecuteTask(task)

	require.NoError(t, err)
	assert.Equal(t, "failed", status.Status)
	assert.Contains(t, status.Stderr, "")
}

func TestExecuteTask_CommandFailed(t *testing.T) {
	engine, err := runner.NewDockerEngine()
	require.NoError(t, err)

	task := queue.Task{
		Name:        "test-command-failed",
		Command:     "ls non_existent_file",
		ResultCheck: "echo 'should not run'",
	}

	status, err := engine.ExecuteTask(task)

	require.NoError(t, err)
	assert.Equal(t, "failed", status.Status)
	assert.NotContains(t, status.Stdout, "should not run")
	assert.Contains(t, status.Stderr, "No such file or directory")
}

func TestExecuteTask_CheckDependsOnCommandEnv(t *testing.T) {
	engine, err := runner.NewDockerEngine()
	require.NoError(t, err)

	task := queue.Task{
		Name:        "test-env-dependency",
		Command:     "export TEST_VAR=123 && echo $TEST_VAR > /tmp/env.txt",
		ResultCheck: "if [ $(cat /tmp/env.txt) -eq 123 ]; then exit 0; else exit 1; fi",
	}

	status, err := engine.ExecuteTask(task)

	require.NoError(t, err)
	assert.Equal(t, "success", status.Status)
}
