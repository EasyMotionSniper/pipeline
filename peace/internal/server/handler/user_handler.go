package handler

import (
	"net/http"
	"pace/internal/common"

	"github.com/gin-gonic/gin"
)

func UserLogin(c *gin.Context) {
	var req common.LoginRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
}
