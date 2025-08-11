package dao

import (
	"pace/internal/server/model"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

var db *gorm.DB

func init() {
	dsn := "root:13616749175ymq@tcp(localhost:3306)/pace?charset=utf8mb4&parseTime=True&loc=Local"
	database, err := gorm.Open(mysql.Open(dsn))
	if err != nil {
		panic(err)
	}
	db = database
	db.AutoMigrate(&model.User{}, &model.Pipeline{}, &model.PipelineVersion{}, &model.PipelineExecution{}, &model.TaskExecution{})
}
