package handler

import (
	"pace/internal/common"
	"pace/internal/server/dao"
	"pace/pkg/api"
	"strconv"

	"github.com/gin-gonic/gin"
)

func ListExecutionHistory(c *gin.Context) {
	historyList, err := dao.NewPipelineExecDao().GetLatestExecutionHistory(c)
	if err != nil {
		common.Error(c, common.NewErrNo(common.GetHistoryFail))
		return
	}
	var versionIDs []uint
	for _, history := range historyList {
		versionIDs = append(versionIDs, history.PipelineVersionID)
	}

	pipelineDao := dao.NewPipelineDao()
	pipelineVersions, err := pipelineDao.GetPipelineVerions(c, versionIDs)
	verisonIDMap := make(map[uint]uint)
	for _, version := range pipelineVersions {
		verisonIDMap[version.ID] = version.PipelineID
	}

	if err != nil {
		common.Error(c, common.NewErrNo(common.GetHistoryFail))
		return
	}

	var historyBriefs []api.ExecutionHistoryBrief
	for _, history := range historyList {
		brief := api.ExecutionHistoryBrief{
			ID:          history.ID,
			Status:      history.Status,
			PipelineID:  verisonIDMap[history.PipelineVersionID],
			TriggerType: history.TriggerType,
			StartTime:   history.CreatedAt.Format("2006-01-02 15:04:05"),
		}
		if brief.Status == "success" || brief.Status == "failed" {
			brief.EndTime = history.UpdatedAt.Format("2006-01-02 15:04:05")
		}
		historyBriefs = append(historyBriefs, brief)
	}

	// first output running, then output other status
	runningList := make([]api.ExecutionHistoryBrief, 0)
	otherList := make([]api.ExecutionHistoryBrief, 0)
	for _, brief := range historyBriefs {
		if brief.Status == "running" {
			runningList = append(runningList, brief)
		} else {
			otherList = append(otherList, brief)
		}
	}
	historyBriefs = append(runningList, otherList...)

	common.Success(c, historyBriefs)
}

func ListExecutionHistoryDetail(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		common.Error(c, common.NewErrNo(common.RequestInvalid))
		return
	}

	history, err := dao.NewPipelineExecDao().GetPipelineExecutionByID(c, uint(id))
	if err != nil {
		common.Error(c, common.NewErrNo(common.GetHistoryDetailFail))
		return
	}

	pipelineVersion, err := dao.NewPipelineDao().GetPipelineVersionById(c, history.PipelineVersionID)
	if err != nil {
		common.Error(c, common.NewErrNo(common.GetHistoryDetailFail))
		return
	}

	taskExecs, err := dao.NewTaskExecDao().GetTaskExecByUUID(c, history.PipelineExecuteUUID)

	if err != nil {
		common.Error(c, common.NewErrNo(common.GetHistoryDetailFail))
		return
	}

	executeDetail := &api.ExecutionHistoryDetail{
		Config: pipelineVersion.Config,
	}

	for _, taskExec := range taskExecs {
		detail := api.TaskDetail{
			TaskName: taskExec.TaskName,
			Status:   taskExec.Status,
			Stdout:   taskExec.Stdout,
			Stderr:   taskExec.Stderr,
		}
		if detail.Status != "skipped" && detail.Status != "pending" {
			detail.StartTime = taskExec.CreatedAt.Format("2006-01-02 15:04:05")
		}
		if detail.Status == "success" || detail.Status == "fail" {
			detail.EndTime = taskExec.UpdatedAt.Format("2006-01-02 15:04:05")
		}
		executeDetail.Tasks = append(executeDetail.Tasks, detail)
	}
	common.Success(c, executeDetail)
}
