package handler

import (
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/internal/server/middleware"

	"github.com/gin-gonic/gin"
)

func UserLogin(c *gin.Context) {
	var req common.LoginRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	userDAO := dao.NewUserDAO()
	user, err := userDAO.GetByUsername(c, req.Username)
	if err != nil {
		common.Error(c, err)
		return
	}
	if user.Password != req.Password {
		common.Error(c, common.NewErrNo(common.PasswordErr))
		return
	}

	token, err := middleware.GenerateJWT(user.Role)
	if err != nil {
		common.Error(c, err)
		return
	}
	c.Header("Authorization", "Bearer "+token)
	common.Success(c, nil)
}
