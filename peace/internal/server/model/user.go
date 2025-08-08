package model

import "gorm.io/gorm"

type User struct {
	gorm.Model
	Username string `gorm:"unique;varchar(255);not null"`
	Password string `gorm:"varchar(255);not null"`
	Role     string `gorm:"type:enum('viewer', 'executor');not null;default:'viewer'"`
}
