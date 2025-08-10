package common

import (
	"errors"
	"fmt"
)

type ErrNo struct {
	ErrCode int    `json:"err_code"`
	ErrMsg  string `json:"err_msg"`
}

const (
	SuccessCode       = 0
	ServiceErr        = 10000
	RequestInvalid    = 10001
	TokenInvalid      = 10002
	PasswordErr       = 10003
	UserNotExists     = 10004
	YamlInvalid       = 10005
	PipelineNotExists = 10006
	PipelineExists    = 10007
	PiplineStartFail  = 10008
)

var errorMsg = map[int]string{
	SuccessCode:       "success",
	ServiceErr:        "service error",
	RequestInvalid:    "request invalid",
	TokenInvalid:      "token invalid",
	PasswordErr:       "password error",
	UserNotExists:     "user not exists",
	YamlInvalid:       "yaml invalid",
	PipelineNotExists: "pipeline not exists",
	PipelineExists:    "pipeline already exists",
	PiplineStartFail:  "pipeline starts fail",
}

func (e ErrNo) Error() string {
	return fmt.Sprintf("err_code=%d, err_msg=%s", e.ErrCode, e.ErrMsg)
}

func NewErrNo(errCode int) error {
	return ErrNo{
		ErrCode: errCode,
		ErrMsg:  errorMsg[errCode],
	}
}

func ConvertErr(err error) ErrNo {
	e := ErrNo{}
	if errors.As(err, &e) {
		return e
	}
	e = ErrNo{
		ErrCode: ServiceErr,
		ErrMsg:  err.Error(),
	}
	return e
}
