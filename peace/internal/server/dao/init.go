package dao

import (
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
}
