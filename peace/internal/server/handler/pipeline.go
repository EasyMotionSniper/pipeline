package handler

import (
	"fmt"
	"log"
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/internal/server/model"
	rpccall "pace/internal/server/rpc_call"
	"pace/pkg/api"
	"pace/pkg/taskrpc"

	"github.com/gin-gonic/gin"
)

// rpc clint
var taskExecRpc *rpccall.Client

func init() {
	return
	client, err := rpccall.NewClient("localhost:8081")
	if err != nil {
		panic(err)
	}
	taskExecRpc = client
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

func TriggerPipeline(c *gin.Context) {
	fmt.Println("TriggerPipeline called")
	var req api.TriggerRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}
	log.Printf("TriggerPipeline req: %v", req)

	pipelineDAO := dao.NewPipelineDao()
	pipeline, err := pipelineDAO.GetNewestVersionByID(c, uint64(req.PipelineID))
	if err != nil {
		common.Error(c, common.NewErrNo(common.PipelineNotExists))
		return
	}

	// unmarshal pipeline config to tasks
	pipelineConfig, err := model.ParsePipelineConfig(pipeline.Config)
	if err != nil {
		common.Error(c, common.NewErrNo(common.YamlInvalid))
		return
	}

	// insert into pipeline execution record
	// TODO
	log.Printf("TriggerPipeline req: %v", req)
	_, err = taskExecRpc.ExecuteTask(&taskrpc.ExecutePipelineRequest{
		ExecutionID: 1,
		Tasks:       pipelineConfig.Tasks,
	})
	if err != nil {
		common.Error(c, common.NewErrNo(common.PiplineStartFail))
		return
	}
	triggerResp := api.TriggerResponse{
		ExecutionID: 1,
	}
	common.Success(c, triggerResp)
}
