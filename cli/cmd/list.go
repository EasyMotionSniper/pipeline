package cmd

import (
	"encoding/json"
	"fmt"

	"pace-cli/client"
	"pace-cli/config"

	"github.com/spf13/cobra"
)

var (
	listPipelineID string
)

// NewListCommand creates the list command
func NewListCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List pipelines or get specific pipeline details",
		Run:   runList,
	}

	cmd.Flags().StringVarP(&listPipelineID, "id", "i", "", "Specific pipeline ID to list")

	return cmd
}

func runList(cmd *cobra.Command, args []string) {
	var url string
	if listPipelineID != "" {
		url = fmt.Sprintf("%s/pipeline/%s", config.ServerURL, listPipelineID)
	} else {
		url = fmt.Sprintf("%s/pipeline", config.ServerURL)
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
	var result interface{}
	if listPipelineID != "" {
		var pipeline client.Pipeline
		if err := json.Unmarshal(body, &pipeline); err != nil {
			fmt.Printf("Error: Failed to parse response - %v\n", err)
			return
		}
		result = pipeline
	} else {
		var pipelines []client.Pipeline
		if err := json.Unmarshal(body, &pipelines); err != nil {
			fmt.Printf("Error: Failed to parse response - %v\n", err)
			return
		}
		result = pipelines
	}

	formatted, err := json.MarshalIndent(result, "", "  ")
	if err != nil {
		fmt.Printf("Error: Failed to format output - %v\n", err)
		return
	}

	fmt.Println(string(formatted))
}
