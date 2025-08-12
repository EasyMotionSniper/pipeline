package test

import (
	"pace/internal/server/model"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
)

func TestInsertPipelineExecution(t *testing.T) {
	db := setupTestDB(t)
	exec := []model.PipelineExecution{
		{
			PipelineVersionID:   1,
			PipelineExecuteUUID: "123",
			TriggerType:         "manual",
			Status:              "running",
		},
		{
			PipelineVersionID:   2,
			PipelineExecuteUUID: "456",
			TriggerType:         "cron",
			Status:              "success",
		},
		{
			PipelineVersionID:   3,
			PipelineExecuteUUID: "789",
			TriggerType:         "webhook",
			Status:              "failed",
			CreatedAt:           time.Now().AddDate(0, 0, -100),
		},
	}
	result := db.Create(&exec)
	require.NoError(t, result.Error)
	require.Equal(t, int64(3), result.RowsAffected)
}

func TestInsertTaskExecution(t *testing.T) {
	db := setupTestDB(t)
	taskExec := []model.TaskExecution{
		{
			TaskName:            "task1",
			PipelineExecuteUUID: "123",
			Status:              "running",
		},
		{
			TaskName:            "task2",
			PipelineExecuteUUID: "123",
			Status:              "success",
			Stdout:              "task2 stdout",
			Stderr:              "task2 stderr",
		},
		{
			TaskName:            "task3",
			PipelineExecuteUUID: "123",
			Status:              "skipped",
		},
		{
			TaskName:            "task1",
			PipelineExecuteUUID: "456",
			Status:              "pending",
		},
		{
			TaskName:            "task2",
			PipelineExecuteUUID: "456",
			Status:              "success",
			Stdout:              "task2 stdout long output",
			Stderr:              "task2 stderr long output",
		},
		{
			TaskName:            "task3",
			PipelineExecuteUUID: "789",
			Status:              "failed",
		},
	}
	result := db.Create(&taskExec)
	require.NoError(t, result.Error)
	require.Equal(t, int64(6), result.RowsAffected)
}
