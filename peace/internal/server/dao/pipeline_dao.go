package dao

import (
	"context"
	"errors"
	"pace/internal/common"
	"pace/internal/server/model"

	"gorm.io/gorm"
)

type PipelineDao interface {
	// create pipeline
	Create(ctx context.Context, pipeline *model.Pipeline, pipelineVersion *model.PipelineVersion) error
	Update(ctx context.Context, id uint, pipeline *model.Pipeline, pipelineVersion *model.PipelineVersion) error
	GetPipelineById(ctx context.Context, id uint) (*model.Pipeline, *model.PipelineVersion, error)
	GetPipelineByName(ctx context.Context, name string) (*model.Pipeline, *model.PipelineVersion, error)
	GetAllPipelines(ctx context.Context) ([]*model.Pipeline, error)
	GetAllPipelineVersions(ctx context.Context) ([]*model.PipelineVersion, error)
	GetPipelineVerions(ctx context.Context, versionIDs []uint) ([]*model.PipelineVersion, error)
	GetPipelineVersionById(ctx context.Context, id uint) (*model.PipelineVersion, error)
}

type pipelineDAO struct {
}

func NewPipelineDao() PipelineDao {
	return &pipelineDAO{}
}

func (d *pipelineDAO) Create(ctx context.Context, pipeline *model.Pipeline, pipelineVersion *model.PipelineVersion) error {
	return db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// create pipeline
		if err := tx.Create(pipeline).Error; err != nil {
			if errors.Is(err, gorm.ErrDuplicatedKey) {
				return common.NewErrNo(common.PipelineExists)
			}
			return err
		}
		// create pipeline version
		pipelineVersion.PipelineID = pipeline.ID
		if err := tx.Create(pipelineVersion).Error; err != nil {
			if errors.Is(err, gorm.ErrDuplicatedKey) {
				return common.NewErrNo(common.PipelineExists)
			}
			return err
		}

		return nil
	})

}

func (d *pipelineDAO) Update(ctx context.Context, id uint, newPipeline *model.Pipeline, pipelineVersion *model.PipelineVersion) error {
	return db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		var pipeline model.Pipeline
		if err := tx.Where("id = ?", id).Take(&pipeline).Error; err != nil {
			return err
		}

		pipeline.Name = newPipeline.Name
		pipeline.Description = newPipeline.Description
		pipeline.LatestVersion += 1
		if err := tx.Save(&pipeline).Error; err != nil {
			return err
		}

		// update pipeline version
		pipelineVersion.PipelineID = pipeline.ID
		pipelineVersion.Version = pipeline.LatestVersion
		if err := tx.Save(pipelineVersion).Error; err != nil {
			return err
		}
		return nil
	})
}

func (d *pipelineDAO) GetPipelineByName(ctx context.Context, name string) (*model.Pipeline, *model.PipelineVersion, error) {
	var pipeline model.Pipeline
	var pipelineVersion model.PipelineVersion
	err := db.WithContext(ctx).Where("name = ?", name).Take(&pipeline).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, nil, common.NewErrNo(common.PipelineNotExists)
		}
		return nil, nil, err
	}
	// get pipeline version
	err = db.WithContext(ctx).Where(&model.PipelineVersion{Version: pipeline.LatestVersion, PipelineID: pipeline.ID}).Take(&pipelineVersion).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, nil, common.NewErrNo(common.PipelineNotExists)
		}
		return nil, nil, err
	}
	return &pipeline, &pipelineVersion, nil
}

func (d *pipelineDAO) GetPipelineById(ctx context.Context, id uint) (*model.Pipeline, *model.PipelineVersion, error) {
	var pipeline model.Pipeline
	var pipelineVersion model.PipelineVersion
	err := db.WithContext(ctx).Where("id = ?", id).Take(&pipeline).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, nil, common.NewErrNo(common.PipelineNotExists)
		}
		return nil, nil, err
	}
	err = db.WithContext(ctx).Where(&model.PipelineVersion{Version: pipeline.LatestVersion, PipelineID: pipeline.ID}).Take(&pipelineVersion).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, nil, common.NewErrNo(common.PipelineNotExists)
		}
		return nil, nil, err
	}
	return &pipeline, &pipelineVersion, nil
}

func (d *pipelineDAO) GetAllPipelines(ctx context.Context) ([]*model.Pipeline, error) {
	var pipelines []*model.Pipeline
	if err := db.WithContext(ctx).Find(&pipelines).Error; err != nil {
		return nil, err
	}
	return pipelines, nil
}

func (d *pipelineDAO) GetAllPipelineVersions(ctx context.Context) ([]*model.PipelineVersion, error) {
	var pipelineVersions []*model.PipelineVersion
	pipelines, err := d.GetAllPipelines(ctx)
	if err != nil {
		return nil, err
	}
	for _, pipeline := range pipelines {
		var pipelineVersion model.PipelineVersion
		if err := db.WithContext(ctx).Where("pipeline_id = ?", pipeline.ID).Where("version = ?", pipeline.LatestVersion).Take(&pipelineVersion).Error; err != nil {
			return nil, err
		}
		pipelineVersions = append(pipelineVersions, &pipelineVersion)
	}

	return pipelineVersions, nil
}

func (d *pipelineDAO) GetPipelineVerions(ctx context.Context, versionIDs []uint) ([]*model.PipelineVersion, error) {
	var pipelineVersions []*model.PipelineVersion
	if err := db.WithContext(ctx).Where("pipeline_id IN ?", versionIDs).Find(&pipelineVersions).Error; err != nil {
		return nil, err
	}
	return pipelineVersions, nil
}

func (d *pipelineDAO) GetPipelineVersionById(ctx context.Context, id uint) (*model.PipelineVersion, error) {
	var pipelineVersion model.PipelineVersion
	if err := db.WithContext(ctx).Where("id = ?", id).Take(&pipelineVersion).Error; err != nil {
		return nil, err
	}
	return &pipelineVersion, nil
}
