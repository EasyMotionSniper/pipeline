package dao

import (
	"context"
	"errors"
	"pace/internal/server/model"
	"time"

	"gorm.io/gorm"
)

type PipelineExecDao interface {
	// create pipeline
	Upsert(ctx context.Context, pipeline *model.PipelineExecution) error
	// get pipeline by name
	GetPipelineExecutionByUUID(ctx context.Context, uuid string) (*model.PipelineExecution, error)
	GetPipelineExecutionByID(ctx context.Context, id uint) (*model.PipelineExecution, error)
	GetLatestExecutionHistory(ctx context.Context) ([]*model.PipelineExecution, error)
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

func (p *pipelineExecDAO) GetPipelineExecutionByUUID(ctx context.Context, uuid string) (*model.PipelineExecution, error) {
	var pipelineExec model.PipelineExecution
	if err := db.WithContext(ctx).Where("pipeline_execution_uuid = ?", uuid).Take(&pipelineExec).Error; err != nil {
		return nil, err
	}
	return &pipelineExec, nil
}

func (p *pipelineExecDAO) GetPipelineExecutionByID(ctx context.Context, id uint) (*model.PipelineExecution, error) {
	var pipelineExec model.PipelineExecution
	if err := db.WithContext(ctx).Where("id = ?", id).Take(&pipelineExec).Error; err != nil {
		return nil, err
	}
	return &pipelineExec, nil
}

func (p *pipelineExecDAO) GetLatestExecutionHistory(ctx context.Context) ([]*model.PipelineExecution, error) {
	var pipelineExecs []*model.PipelineExecution
	// get last 90 days execution history
	if err := db.WithContext(ctx).Where("created_at > ?", time.Now().AddDate(0, 0, -90)).Order("created_at desc").Find(&pipelineExecs).Error; err != nil {
		return nil, err
	}
	return pipelineExecs, nil
}
