package handler

import (
	"fmt"
	"log"
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/internal/server/model"
	"pace/internal/server/scheduler"
	"pace/pkg/api"
	"pace/pkg/queue"

	"github.com/gin-gonic/gin"
)

func CreatePipeline(c *gin.Context) {
	// read yaml
	yamlContent, err := c.GetRawData()
	if err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	pipelineConfig, err := queue.ParsePipelineConfig(string(yamlContent))
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}
	fmt.Println(pipelineConfig)

	// create pipeline
	pipelineDAO := dao.NewPipelineDao()
	pipeline := &model.Pipeline{
		Name:          pipelineConfig.Name,
		Description:   pipelineConfig.Description,
		LatestVersion: 0,
	}
	pipelineVersion := &model.PipelineVersion{
		Version: 0,
		Config:  string(yamlContent),
	}
	err = pipelineDAO.Create(c, pipeline, pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineExists))
		return
	}

	err = scheduler.GetSchedulerService().UpsertPipelineSchedule(pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.ScheduleInvalid))
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

	pipelineConfig, err := queue.ParsePipelineConfig(string(yamlContent))
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}

	pipelineDAO := dao.NewPipelineDao()
	pipeline := &model.Pipeline{
		Name:        pipelineConfig.Name,
		Description: pipelineConfig.Description,
	}
	// check if pipeline exists
	pipelineVersion := &model.PipelineVersion{
		Config: string(yamlContent),
	}
	err = pipelineDAO.Update(c, name, pipeline, pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineNotExists))
		return
	}
	err = scheduler.GetSchedulerService().UpsertPipelineSchedule(pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.ScheduleInvalid))
		return
	}

	common.Success(c, nil)
}

func TriggerPipeline(c *gin.Context) {
	var req api.TriggerRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}
	log.Printf("TriggerPipeline req: %v", req)

	pipelineDAO := dao.NewPipelineDao()
	_, pipelineVersion, err := pipelineDAO.GetPipelineByName(c, req.PipelineName)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineNotExists))
		return
	}

	// unmarshal pipeline config to tasks
	pipelineConfig, err := queue.ParsePipelineConfig(pipelineVersion.Config)
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}
	fmt.Println(pipelineConfig)

	// insert into pipeline execution record
	// TODO

	triggerResp := api.TriggerResponse{
		ExecutionID: 1,
	}
	common.Success(c, triggerResp)
}
