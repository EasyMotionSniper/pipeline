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

	cmd.Flags().IntP("id", "i", 0, "Pipeline ID to trigger (required)")
	cmd.MarkFlagRequired("id")

	return cmd
}

func runTrigger(cmd *cobra.Command, args []string) {
	triggerPipelineID, err := cmd.Flags().GetInt("id")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	req := api.TriggerRequest{
		PipelineID: triggerPipelineID,
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

	if triggerResp.Code != common.SuccessCode {
		fmt.Printf("Trigger failed: %s\n", triggerResp.Message)
		return
	}

	var triggerRespData api.TriggerResponse
	if err := json.Unmarshal(triggerResp.Data.([]byte), &triggerRespData); err != nil {
		fmt.Printf("Error: Failed to deserialize response data - %v\n", err)
		return
	}

	fmt.Printf("Successfully triggered pipeline with exec ID %d\n", triggerRespData.ExecutionID)
}
