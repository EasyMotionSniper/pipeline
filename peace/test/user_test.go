package test

import (
	"pace/internal/server/model"
	"testing"

	"github.com/stretchr/testify/require"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

func setupTestDB(t *testing.T) *gorm.DB {
	dsn := "root:13616749175ymq@tcp(localhost:3306)/pace?charset=utf8mb4&parseTime=True&loc=Local"
	db, err := gorm.Open(mysql.Open(dsn))
	require.NoError(t, err)

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
	require.NoError(t, result.Error)
	require.Equal(t, int64(2), result.RowsAffected)

}
