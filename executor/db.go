package executor

import (
	"yourproject/models"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

var DB *gorm.DB

// Init 初始化数据库连接
func Init(path string) error {
	var err error
	DB, err = gorm.Open(sqlite.Open(path), &gorm.Config{})
	if err != nil {
		return err
	}

	// 迁移数据表
	return DB.AutoMigrate(
		&models.Pipeline{},
		&models.Trigger{},
		&models.Task{},
		&models.PipelineExecution{},
		&models.TaskExecution{},
	)
}

// CreateOrUpdatePipeline 创建或更新流水线，保存历史版本
func CreateOrUpdatePipeline(pipeline *models.Pipeline) error {
	// 查找现有流水线
	var existing models.Pipeline
	result := DB.Where("name = ?", pipeline.Name).First(&existing)

	if result.Error == gorm.ErrRecordNotFound {
		// 新流水线，版本号设为1
		pipeline.Version = 1
		return DB.Create(pipeline).Error
	} else if result.Error != nil {
		return result.Error
	}

	// 现有流水线，创建新版本
	pipeline.ID = 0 // 重置ID，创建新记录
	pipeline.Version = existing.Version + 1
	return DB.Create(pipeline).Error
}

// GetLatestPipeline 获取最新版本的流水线
func GetLatestPipeline(name string) (*models.Pipeline, error) {
	var pipeline models.Pipeline
	result := DB.Where("name = ?", name).
		Order("version DESC").
		First(&pipeline)

	if result.Error != nil {
		return nil, result.Error
	}

	// 加载关联的triggers和tasks
	DB.Model(&pipeline).Association("Triggers").Find(&pipeline.Triggers)
	DB.Model(&pipeline).Association("Tasks").Find(&pipeline.Tasks)

	return &pipeline, nil
}

// GetPipelineByVersion 获取指定版本的流水线
func GetPipelineByVersion(name string, version int) (*models.Pipeline, error) {
	var pipeline models.Pipeline
	result := DB.Where("name = ? AND version = ?", name, version).First(&pipeline)

	if result.Error != nil {
		return nil, result.Error
	}

	// 加载关联的triggers和tasks
	DB.Model(&pipeline).Association("Triggers").Find(&pipeline.Triggers)
	DB.Model(&pipeline).Association("Tasks").Find(&pipeline.Tasks)

	return &pipeline, nil
}

// CreatePipelineExecution 创建流水线执行记录
func CreatePipelineExecution(exec *models.PipelineExecution) error {
	return DB.Create(exec).Error
}

// UpdatePipelineExecution 更新流水线执行记录
func UpdatePipelineExecution(exec *models.PipelineExecution) error {
	return DB.Save(exec).Error
}

// CreateTaskExecution 创建任务执行记录
func CreateTaskExecution(exec *models.TaskExecution) error {
	return DB.Create(exec).Error
}

// UpdateTaskExecution 更新任务执行记录
func UpdateTaskExecution(exec *models.TaskExecution) error {
	return DB.Save(exec).Error
}
