package cmd

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"pace/internal/cli/client"
	"pace/internal/common"
	"pace/pkg/api"

	"github.com/spf13/cobra"
)

// NewTriggerCommand creates the trigger command
func NewTriggerCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "trigger",
		Short: "Trigger a pipeline execution",
		Run:   runTrigger,
	}

	cmd.Flags().StringP("name", "n", "", "Pipeline name to trigger (required)")
	cmd.MarkFlagRequired("name")

	return cmd
}

func runTrigger(cmd *cobra.Command, args []string) {
	pipelineName, err := cmd.Flags().GetString("name")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	req := api.TriggerRequest{
		PipelineName: pipelineName,
	}
	jsonData, err := json.Marshal(req)
	if err != nil {
		fmt.Printf("Error: Failed to serialize data - %v\n", err)
		return
	}

	resp, err := client.SendRequest(http.MethodPost, "/trigger", bytes.NewBuffer(jsonData))
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	defer resp.Body.Close()

	// Handle response
	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

	if resp.StatusCode != http.StatusOK {
		fmt.Printf("Trigger failed: %s\n", string(body))
		return
	}

	var triggerResp common.Response
	if err := json.Unmarshal(body, &triggerResp); err != nil {
		fmt.Printf("Error: Failed to deserialize response - %v\n", err)
		return
	}

	if triggerResp.Code != common.SUCCESS {
		fmt.Printf("Trigger failed: %s\n", triggerResp.Message)
		return
	}

}
