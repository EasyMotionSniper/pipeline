package dao

import (
	"context"
	"errors"
	"pace/internal/server/model"

	"gorm.io/gorm"
)

type TaskExecDao interface {
	Upsert(ctx context.Context, pipeline *model.TaskExecution) error
	// get pipeline by name
	GetTaskExecByUUID(ctx context.Context, uuid string) ([]*model.TaskExecution, error)
}

type taskExecDAO struct {
}

func NewTaskExecDao() TaskExecDao {
	return &taskExecDAO{}
}

func (p *taskExecDAO) Upsert(ctx context.Context, newTaskExec *model.TaskExecution) error {
	return db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		var taskExec model.TaskExecution
		if err := tx.Where("pipeline_execute_uuid = ? AND task_name = ?", newTaskExec.PipelineExecuteUUID, newTaskExec.TaskName).Take(&taskExec).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return tx.Create(newTaskExec).Error
			}
			return err
		}

		taskExec.Status = newTaskExec.Status
		taskExec.Stdout = newTaskExec.Stdout
		taskExec.Stderr = newTaskExec.Stderr

		return tx.Save(&taskExec).Error
	})
}

func (p *taskExecDAO) GetTaskExecByUUID(ctx context.Context, uuid string) ([]*model.TaskExecution, error) {
	var taskExec []*model.TaskExecution
	if err := db.WithContext(ctx).Where("pipeline_execute_uuid = ?", uuid).Find(&taskExec).Error; err != nil {
		return nil, err
	}
	return taskExec, nil
}
