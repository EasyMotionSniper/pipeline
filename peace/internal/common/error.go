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
	SUCCESS     = 0
	SERVICE_ERR = iota + 10000
	REQUEST_INVALID
	TOKEN_INVALID
	PASSWORD_ERR
	USER_NOT_EXISTS
	YAML_INVALID
	PIPELINE_NOT_EXISTS
	PIPELINE_EXISTS
	PIPELINE_START_FAIL
	SCHEDULE_INVALID
	GET_HISTORY_FAIL
	GET_HISTORY_DETAIL_FAIL
	WEBHOOK_INVALID
)

var errorMsg = map[int]string{
	SUCCESS:                 "success",
	SERVICE_ERR:             "service error",
	REQUEST_INVALID:         "request invalid",
	TOKEN_INVALID:           "token invalid",
	PASSWORD_ERR:            "password error",
	USER_NOT_EXISTS:         "user not exists",
	YAML_INVALID:            "yaml invalid",
	PIPELINE_NOT_EXISTS:     "pipeline not exists",
	PIPELINE_EXISTS:         "pipeline already exists",
	PIPELINE_START_FAIL:     "pipeline starts fail",
	SCHEDULE_INVALID:        "schedule invalid",
	GET_HISTORY_FAIL:        "get history fail",
	GET_HISTORY_DETAIL_FAIL: "get history detail fail",
	WEBHOOK_INVALID:         "webhook invalid",
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
		ErrCode: SERVICE_ERR,
		ErrMsg:  err.Error(),
	}
	return e
}
