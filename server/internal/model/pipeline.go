package model

import "gorm.io/gorm"

type Pipeline struct {
	gorm.Model
	Name   string `gorm:"unique;not null"`
	Config string `gorm:"type:text;not null"`
}
