package cmd

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"

	"pace-cli/client"
	"pace-cli/config"

	"github.com/spf13/cobra"
)

var (
	triggerPipelineID string
)

// NewTriggerCommand creates the trigger command
func NewTriggerCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "trigger",
		Short: "Trigger a pipeline execution",
		Run:   runTrigger,
	}

	cmd.Flags().StringVarP(&triggerPipelineID, "id", "i", "", "Pipeline ID to trigger (required)")
	cmd.MarkFlagRequired("id")

	return cmd
}

func runTrigger(cmd *cobra.Command, args []string) {
	url := fmt.Sprintf("%s/trigger", config.ServerURL)

	// Prepare trigger data
	data := map[string]string{
		"pipeline_id": triggerPipelineID,
	}
	jsonData, err := json.Marshal(data)
	if err != nil {
		fmt.Printf("Error: Failed to serialize data - %v\n", err)
		return
	}

	// Create request
	req, err := client.CreateAuthenticatedRequest("POST", url, bytes.NewBuffer(jsonData))
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	req.Header.Set("Content-Type", "application/json")

	// Send request
	resp, err := client.DoRequest(req)
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

	fmt.Printf("Successfully triggered pipeline %s\n", triggerPipelineID)
	fmt.Println(string(body))
}
