package cmd

import (
	"encoding/json"
	"fmt"
	"net/http"
	"pace/internal/cli/client"
	"pace/internal/common"
	"pace/pkg/api"

	"github.com/spf13/cobra"
)

// NewListCommand creates the list command
func NewListCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List pipelines or get specific pipeline details",
		Run:   runList,
	}

	cmd.Flags().IntP("id", "i", 0, "Specific pipeline id to list")

	return cmd
}

func runList(cmd *cobra.Command, args []string) {
	listPipelineId, _ := cmd.Flags().GetInt("id")
	var path string
	isBrief := true
	if listPipelineId != 0 {
		isBrief = false
		path = fmt.Sprintf("/pipeline/%d", listPipelineId)
	} else {
		path = "/pipeline"
	}

	resp, err := client.SendRequest(http.MethodGet, path, nil)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	defer resp.Body.Close()

	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

	var listResp common.Response
	if err := json.Unmarshal(body, &listResp); err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	if listResp.Code != 0 {
		fmt.Printf("Error: %s\n", listResp.Message)
		return
	}
	dataBytes, err := json.Marshal(listResp.Data)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	if isBrief {
		var pipelines []api.PipelineBrief

		if err := json.Unmarshal(dataBytes, &pipelines); err != nil {
			fmt.Printf("Error: %v\n", err)
			return
		}
		printPipelinesBrief(pipelines)
	} else {
		var pipelineDetail api.PipelineDetail
		if err := json.Unmarshal(dataBytes, &pipelineDetail); err != nil {
			fmt.Printf("Error: %v\n", err)
			return
		}
		printPipelineDetail(pipelineDetail)
	}
}

func printPipelinesBrief(pipelines []api.PipelineBrief) {
	if len(pipelines) == 0 {
		fmt.Println("No pipelines found.")
		return
	}
	fmt.Printf("%-5s %-20s %-50s\n", "ID", "NAME", "DESCRIPTION")
	for _, p := range pipelines {
		fmt.Printf("%-5d %-20s %-50s\n", p.ID, p.Name, p.Description)
	}
}

func printPipelineDetail(pipeline api.PipelineDetail) {
	fmt.Printf("%s\n", pipeline.Config)
}
