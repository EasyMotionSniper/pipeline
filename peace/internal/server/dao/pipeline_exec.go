package dao

import (
	"context"
	"errors"
	"pace/internal/server/model"

	"gorm.io/gorm"
)

type PipelineExecDao interface {
	// create pipeline
	Upsert(ctx context.Context, pipeline *model.PipelineExecution) error
	// get pipeline by name
	GetPipelineByUUID(ctx context.Context, uuid string) (*model.PipelineExecution, error)
}

type pipelineExecDAO struct {
}

func NewPipelineExecDao() PipelineExecDao {
	return &pipelineExecDAO{}
}

func (p *pipelineExecDAO) Upsert(ctx context.Context, pipeline *model.PipelineExecution) error {
	return db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		var pipelineExec model.PipelineExecution
		if err := tx.Where("pipeline_execution_uuid = ?", pipeline.PipelineExecuteUUID).Take(&pipelineExec).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return tx.Create(pipeline).Error
			}
			return err
		}

		pipelineExec.Status = pipeline.Status
		return tx.Save(&pipelineExec).Error
	})
}

func (p *pipelineExecDAO) GetPipelineByUUID(ctx context.Context, uuid string) (*model.PipelineExecution, error) {
	var pipelineExec model.PipelineExecution
	if err := db.WithContext(ctx).Where("pipeline_execution_uuid = ?", uuid).Take(&pipelineExec).Error; err != nil {
		return nil, err
	}
	return &pipelineExec, nil
}
