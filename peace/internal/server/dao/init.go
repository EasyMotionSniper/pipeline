package dao

import (
	"fmt"
	"pace/internal/common"
	"pace/internal/server/model"
	"strconv"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

var db *gorm.DB

func init() {
	config := common.GetConfig()
	dsn := config.DBUser + ":" + config.DBPassword + "@tcp(" + config.DBHost + ":" + strconv.Itoa(config.DBPort) + ")/" + config.DBName + "?charset=utf8mb4&parseTime=True&loc=Local"
	fmt.Println(dsn)
	database, err := gorm.Open(mysql.Open(dsn))
	if err != nil {
		panic(err)
	}
	db = database
	db.AutoMigrate(&model.User{}, &model.Pipeline{}, &model.PipelineVersion{}, &model.PipelineExecution{}, &model.TaskExecution{})
}
