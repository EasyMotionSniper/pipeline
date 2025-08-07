package dao

import (
	"context"
	"errors"
	"fmt"
	"pace/internal/common"
	"pace/internal/server/model"

	"gorm.io/gorm"
)

type UserDAO interface {
	// create user
	Create(ctx context.Context, user *model.User) error
	// get user by id
	GetByID(ctx context.Context, id uint64) (*model.User, error)
	// get user by username
	GetByUsername(ctx context.Context, username string) (*model.User, error)
}

type userDAO struct {
}

func NewUserDAO() UserDAO {
	return &userDAO{}
}

func (d *userDAO) Create(ctx context.Context, user *model.User) error {
	return db.WithContext(ctx).Create(user).Error
}

func (d *userDAO) GetByID(ctx context.Context, id uint64) (*model.User, error) {
	var user model.User
	err := db.WithContext(ctx).Where("id = ?", id).Take(&user).Error
	if err != nil {
		return nil, err
	}
	return &user, nil
}

func (d *userDAO) GetByUsername(ctx context.Context, username string) (*model.User, error) {
	var user model.User
	err := db.WithContext(ctx).Where("username = ?", username).Take(&user).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			fmt.Println("not found user")
			return nil, common.NewErrNo(common.UserNotExists)
		}
		fmt.Println(err)
		return nil, err
	}
	return &user, nil
}
