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
	"strconv"

	"github.com/gin-gonic/gin"
)

func CreatePipeline(c *gin.Context) {
	// read yaml
	yamlContent, err := c.GetRawData()
	if err != nil {
		common.Error(c, common.NewErrNo(common.REQUEST_INVALID))
		return
	}

	pipelineConfig, err := queue.ParsePipelineConfig(string(yamlContent))
	if err != nil {
		common.Error(c, common.NewErrNo(common.YAML_INVALID))
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
		common.Error(c, common.NewErrNo(common.PIPELINE_EXISTS))
		return
	}

	err = scheduler.GetSchedulerService().UpsertPipelineSchedule(pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.SCHEDULE_INVALID))
		return
	}

	common.Success(c, nil)
}

func UpdatePipeline(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		common.Error(c, common.NewErrNo(common.REQUEST_INVALID))
		return
	}

	yamlContent, err := c.GetRawData()
	if err != nil {
		common.Error(c, common.NewErrNo(common.REQUEST_INVALID))
		return
	}

	pipelineConfig, err := queue.ParsePipelineConfig(string(yamlContent))
	if err != nil {
		common.Error(c, common.NewErrNo(common.YAML_INVALID))
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
	err = pipelineDAO.Update(c, uint(id), pipeline, pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PIPELINE_EXISTS))
		return
	}
	err = scheduler.GetSchedulerService().UpsertPipelineSchedule(pipelineVersion)
	if err != nil {
		common.Error(c, common.NewErrNo(common.SCHEDULE_INVALID))
		return
	}

	common.Success(c, nil)
}

func TriggerPipeline(c *gin.Context) {
	var req api.TriggerRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		common.Error(c, common.NewErrNo(common.REQUEST_INVALID))
		return
	}
	log.Printf("TriggerPipeline req: %v", req)

	pipelineDAO := dao.NewPipelineDao()
	_, pipelineVersion, err := pipelineDAO.GetPipelineByName(c, req.PipelineName)
	if err != nil {
		common.Error(c, common.NewErrNo(common.PIPELINE_NOT_EXISTS))
		return
	}

	// unmarshal pipeline config to tasks
	pipelineConfig, err := queue.ParsePipelineConfig(pipelineVersion.Config)
	if err != nil {
		common.Error(c, common.NewErrNo(common.YAML_INVALID))
		return
	}
	fmt.Println(pipelineConfig)

	// insert into pipeline execution record
	// TODO

	common.Success(c, nil)
}

func ListPipelines(c *gin.Context) {
	pipelineDAO := dao.NewPipelineDao()
	pipelines, err := pipelineDAO.GetAllPipelines(c)
	if err != nil {
		common.Error(c, common.NewErrNo(common.GET_HISTORY_FAIL))
		return
	}
	var pipelinesBrief []api.PipelineBrief
	for _, pipeline := range pipelines {
		pipelinesBrief = append(pipelinesBrief, api.PipelineBrief{
			ID:          pipeline.ID,
			Name:        pipeline.Name,
			Description: pipeline.Description,
		})
	}
	common.Success(c, pipelinesBrief)
}

func ListPipelineDetail(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		common.Error(c, common.NewErrNo(common.REQUEST_INVALID))
		return
	}
	pipelineDAO := dao.NewPipelineDao()
	_, pipelineVersion, err := pipelineDAO.GetPipelineById(c, uint(id))
	if err != nil {
		common.Error(c, common.NewErrNo(common.PIPELINE_NOT_EXISTS))
		return
	}

	pipelineDetail := api.PipelineDetail{
		Config: pipelineVersion.Config,
	}
	common.Success(c, pipelineDetail)
}
