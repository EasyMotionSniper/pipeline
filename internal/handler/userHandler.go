package handler

import (
	"net/http"

	"github.com/gin-gonic/gin"
)

func UserLogin(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{
		"message": "Login success",
	})
}
