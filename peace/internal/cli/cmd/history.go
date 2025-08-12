package cmd

import (
	"encoding/json"
	"fmt"
	"pace/internal/cli/client"
	"pace/internal/common"
	"pace/pkg/api"

	"github.com/spf13/cobra"
)

func NewHistoryCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "history",
		Short: "Show pipeline execution history",
		Run:   runHistory,
	}

	cmd.Flags().IntP("id", "i", 0, "Specific pipeline ID to show history")

	return cmd
}

func runHistory(cmd *cobra.Command, args []string) {
	historyPipelineID, _ := cmd.Flags().GetInt("id")
	brief := true
	var path string
	if historyPipelineID != 0 {
		brief = false
		path = fmt.Sprintf("/history/%d", historyPipelineID)
	} else {
		path = "/history"
	}

	resp, err := client.SendRequest("GET", path, nil)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	var historyResp common.Response
	if err := json.Unmarshal(body, &historyResp); err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	if historyResp.Code != 0 {
		fmt.Printf("Error: %s\n", historyResp.Message)
		return
	}
	dataBytes, err := json.Marshal(historyResp.Data)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	if brief {
		var historyList []api.ExecutionHistoryBrief
		if err := json.Unmarshal(dataBytes, &historyList); err != nil {
			fmt.Printf("Error: %v\n", err)
			return
		}
		printHistoryList(historyList)
		return
	} else {
		var executionHistoryDetail api.ExecutionHistoryDetail
		if err := json.Unmarshal(dataBytes, &executionHistoryDetail); err != nil {
			fmt.Printf("Error: %v\n", err)
			return
		}
		printHistoryDetail(executionHistoryDetail)
		return
	}
}

func printHistoryList(historyList []api.ExecutionHistoryBrief) {
	if len(historyList) == 0 {
		fmt.Println("No execution history found.")
		return
	}
	fmt.Printf("%-5s %-15s %-15s %-15s %-20s %-20s\n",
		"ID", "PipelineID", "Status", "TriggerType", "StartTime", "EndTime")

	for _, history := range historyList {
		fmt.Printf("%-5d %-15d %-15s %-15s %-20s %-20s\n",
			history.ID, history.PipelineID, history.Status,
			history.TriggerType, history.StartTime, history.EndTime)
	}
}

func printHistoryDetail(historyDetail api.ExecutionHistoryDetail) {
	if len(historyDetail.Tasks) == 0 {
		fmt.Println("No tasks in this execution.")
		return
	}
	fmt.Printf("\nConfig:\n%s\n", historyDetail.Config)

	fmt.Printf("\n%-10s %-10s %-20s %-20s %-20s %-20s\n",
		"TaskName", "Status",
		"Stdout", "Stderr", "StartTime", "EndTime")

	for _, task := range historyDetail.Tasks {

		fmt.Printf("%-10s %-10s %-20s %-20s %-20s %-20s\n",
			task.TaskName, task.Status,
			task.Stdout, task.Stderr, task.StartTime, task.EndTime)
	}
}
