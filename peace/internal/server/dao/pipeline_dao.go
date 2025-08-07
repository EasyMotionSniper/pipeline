package dao

import (
	"context"
	"errors"
	"fmt"
	"pace/internal/common"
	"pace/internal/server/model"

	"gorm.io/gorm"
)

type PipelineDao interface {
	// create pipeline
	Create(ctx context.Context, pipeline *model.Pipeline) error
	// get pipeline by id
	GetByID(ctx context.Context, id uint64) (*model.Pipeline, error)
	// get pipeline by name
	GetByName(ctx context.Context, name string) (*model.Pipeline, error)
}

type pipelineDAO struct {
}

func NewPipelineDao() PipelineDao {
	return &pipelineDAO{}
}

func (d *pipelineDAO) Create(ctx context.Context, pipeline *model.Pipeline) error {
	return db.WithContext(ctx).Create(pipeline).Error
}

func (d *pipelineDAO) GetByID(ctx context.Context, id uint64) (*model.Pipeline, error) {
	var pipeline model.Pipeline
	err := db.WithContext(ctx).Where("id = ?", id).Take(&pipeline).Error
	if err != nil {
		return nil, err
	}
	return &pipeline, nil
}

func (d *pipelineDAO) GetByName(ctx context.Context, name string) (*model.Pipeline, error) {
	var pipeline model.Pipeline
	err := db.WithContext(ctx).Where("name = ?", name).Take(&pipeline).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			fmt.Println("not found pipeline")
			return nil, common.NewErrNo(common.PipelineNotExists)
		}
		fmt.Println(err)
		return nil, err
	}
	return &pipeline, nil
}
