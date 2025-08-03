package test

import (
	"pace/internal/model"
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

	if err != nil {
		t.Fatalf("Migration failed: %v", err)
	}

	return db
}
func TestBatchPipelineInsert(t *testing.T) {
	db := setupTestDB(t)
	pipelines := []model.Pipeline{
		{Name: "Pipeline A", Config: "config_a"},
		{Name: "Pipeline B", Config: "config_b"},
	}

	result := db.Create(&pipelines)
	if result.Error != nil {
		t.Fatalf("Batch insert failed: %v", result.Error)
	}

	// 验证插入数量
	if result.RowsAffected != 2 {
		t.Errorf("Expected 2 rows, got %d", result.RowsAffected)
	}

}
