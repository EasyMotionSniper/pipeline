package handler

import (
	"fmt"
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/internal/server/model"

	"github.com/gin-gonic/gin"
)

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

	// create pipeline
	pipelineDAO := dao.NewPipelineDao()
	pipeline := &model.Pipeline{
		Name:        pipelineConfig.Name,
		Description: pipelineConfig.Description,
		Version:     0,
		Config:      string(yamlContent),
	}
	err = pipelineDAO.Create(c, pipeline)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineExists))
		return
	}

	common.Success(c, nil)
}

func UpdatePipeline(c *gin.Context) {
	name := c.Param("name")
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

	pipelineDAO := dao.NewPipelineDao()
	pipeline, err := pipelineDAO.GetNewestVersionByName(c, name)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineNotExists))
		return
	}
	newPipeline := &model.Pipeline{
		Name:        pipelineConfig.Name,
		Description: pipelineConfig.Description,
		Version:     pipeline.Version + 1,
		Config:      string(yamlContent),
	}
	err = pipelineDAO.Create(c, newPipeline)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineExists))
		return
	}

	common.Success(c, nil)
}
