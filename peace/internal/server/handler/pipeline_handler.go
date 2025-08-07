package handler

import (
	"fmt"
	"pace/internal/common"
	"pace/internal/server/model"

	"github.com/gin-gonic/gin"
)

func UpdatePipeline(c *gin.Context) {
	// name := c.Param("name")
}

func CreatePipeline(c *gin.Context) {
	// read yaml
	yamlContent, err := c.GetRawData()
	if err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	pipelineConfig, err := model.ParsePipelineConfig(string(yamlContent))
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}
	fmt.Println(pipelineConfig)

}
