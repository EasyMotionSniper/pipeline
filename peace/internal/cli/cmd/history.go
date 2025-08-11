package cmd

import (
	"encoding/json"
	"fmt"
	"pace/internal/cli/client"

	"github.com/spf13/cobra"
)

// NewHistoryCommand creates the history command
func NewHistoryCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "history",
		Short: "Show pipeline execution history",
		Run:   runHistory,
	}

	cmd.Flags().StringP("id", "i", "", "Specific pipeline ID to show history for")

	return cmd
}

func runHistory(cmd *cobra.Command, args []string) {
	historyPipelineID, err := cmd.Flags().GetString("id")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	var path string
	if historyPipelineID != "" {
		path = fmt.Sprintf("/history/%s", historyPipelineID)
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
