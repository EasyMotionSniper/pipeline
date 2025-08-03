package cmd

import (
	"encoding/json"
	"fmt"

	"pace-cli/client"
	"pace-cli/config"

	"github.com/spf13/cobra"
)

var (
	historyPipelineID string
)

// NewHistoryCommand creates the history command
func NewHistoryCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "history",
		Short: "Show pipeline execution history",
		Run:   runHistory,
	}

	cmd.Flags().StringVarP(&historyPipelineID, "id", "i", "", "Specific pipeline ID to show history for")

	return cmd
}

func runHistory(cmd *cobra.Command, args []string) {
	var url string
	if historyPipelineID != "" {
		url = fmt.Sprintf("%s/history/%s", config.ServerURL, historyPipelineID)
	} else {
		url = fmt.Sprintf("%s/history", config.ServerURL)
	}

	// Create request
	req, err := client.CreateAuthenticatedRequest("GET", url, nil)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

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

	// Format and display result
	var history []client.ExecutionHistory
	if err := json.Unmarshal(body, &history); err != nil {
		fmt.Printf("Error: Failed to parse response - %v\n", err)
		return
	}

	formatted, err := json.MarshalIndent(history, "", "  ")
	if err != nil {
		fmt.Printf("Error: Failed to format output - %v\n", err)
		return
	}

	fmt.Println(string(formatted))
}
