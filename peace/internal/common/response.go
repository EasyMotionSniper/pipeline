package common

import (
	"net/http"

	"github.com/gin-gonic/gin"
)

type Response struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    any    `json:"data"`
}

func Success(c *gin.Context, data any) {
	c.JSON(http.StatusOK, Response{
		Code:    SuccessCode,
		Message: errorMsg[SuccessCode],
		Data:    data,
	})
}
func Error(c *gin.Context, err error) {
	e := ConvertErr(err)
	c.JSON(http.StatusOK, Response{
		Code:    e.ErrCode,
		Message: e.ErrMsg,
		Data:    nil,
	})
}
