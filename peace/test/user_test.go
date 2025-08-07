package test

import (
	"pace/internal/server/model"
	"testing"

	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

func setupTestDB(t *testing.T) *gorm.DB {
	dsn := "root:13616749175ymq@tcp(localhost:3306)/pace?charset=utf8mb4&parseTime=True&loc=Local"
	db, err := gorm.Open(mysql.Open(dsn))
	if err != nil {
		t.Fatalf("Failed to connect database: %v", err)
	}

	return db
}
func TestUserCreate(t *testing.T) {
	db := setupTestDB(t)
	user := []model.User{
		{
			Username: "yang",
			Password: "12345",
			Role:     "executor",
		},
		{
			Username: "wang",
			Password: "123",
			Role:     "viewer",
		},
	}

	result := db.Create(user)
	if result.Error != nil {
		t.Fatalf("Batch insert failed: %v", result.Error)
	}

}
