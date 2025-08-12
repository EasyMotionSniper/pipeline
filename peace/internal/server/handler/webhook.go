package handler

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/pkg/queue"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
)

type WebhookPayload struct {
	Name string `json:"name" binding:"required"`
}

const (
	webhookSecret   = "shared_secret"
	timestampMaxAge = 300
)

func Webhook(c *gin.Context) {
	timestampStr := c.GetHeader("X-Webhook-Timestamp")
	signature := c.GetHeader("X-Webhook-Signature")

	if timestampStr == "" || signature == "" {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	timestamp, err := strconv.ParseInt(timestampStr, 10, 64)
	if err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	now := time.Now().Unix()
	if now-timestamp > timestampMaxAge || timestamp > now {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	var payload WebhookPayload
	if err := c.ShouldBindJSON(&payload); err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	signatureBase := fmt.Sprintf("%s.%s.%s", timestampStr, string(payloadBytes), webhookSecret)
	fmt.Printf("signatureBase: %s\n", signatureBase)

	hash := sha256.Sum256([]byte(signatureBase))
	computedSignature := hex.EncodeToString(hash[:])

	if computedSignature != signature {
		fmt.Printf("computedSignature: %s, signature: %s\n", computedSignature, signature)
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	_, pipelineVersion, err := dao.NewPipelineDao().GetPipelineByName(c, payload.Name)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineNotExists))
		return
	}

	// check config
	pipelineConf, err := queue.ParsePipelineConfig(pipelineVersion.Config)
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}
	ok := false
	for _, trigger := range pipelineConf.Triggers {
		// current url
		if trigger.Webhook == c.Request.URL.String() {
			ok = true
			break
		}
	}
	if !ok {
		common.Error(c, common.NewErrNo(common.WebhookInvalid))
		return
	}
	common.Success(c, nil)
}
