package cmd

import (
	"encoding/json"
	"fmt"
	"net/http"
	"pace/internal/cli/client"

	"github.com/spf13/cobra"
)

// NewListCommand creates the list command
func NewListCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List pipelines or get specific pipeline details",
		Run:   runList,
	}

	cmd.Flags().StringP("id", "i", "", "Specific pipeline ID to list")

	return cmd
}

func runList(cmd *cobra.Command, args []string) {
	listPipelineID, err := cmd.Flags().GetString("id")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	var path string
	if listPipelineID != "" {
		path = fmt.Sprintf("/pipeline/%s", listPipelineID)
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
