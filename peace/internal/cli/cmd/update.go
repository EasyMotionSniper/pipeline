package cmd

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"pace/internal/cli/client"
	"pace/internal/common"

	"github.com/spf13/cobra"
)

var (
	configName string
	yamlFile   string
)

func NewCreateCommand() *cobra.Command {

	cmd := &cobra.Command{
		Use:   "create",
		Short: "Create a new pipeline from a YAML file",
		Run:   runCreateCommand,
	}
	cmd.Flags().StringVarP(&yamlFile, "yaml_file", "f", "", "YAML file path (required)")
	cmd.MarkFlagRequired("yaml_file")
	return cmd
}

func NewUpdateCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "update",
		Short: "Update a pipeline with a new YAML configuration",
		Run:   runUpdateCommand,
	}
	cmd.Flags().StringVarP(&configName, "config_name", "n", "", "Config name (required)")
	cmd.Flags().StringVarP(&yamlFile, "yaml_file", "f", "", "YAML file path (required)")
	cmd.MarkFlagRequired("config_name")
	cmd.MarkFlagRequired("yaml_file")
	return cmd
}

func runCreateCommand(cmd *cobra.Command, args []string) {
	// read yaml file
	fileContent, err := os.ReadFile(yamlFile)
	if err != nil {
		fmt.Println("Error reading YAML file:", err)
		return
	}

	resp, err := client.SendFile(http.MethodPost, "/pipeline/create", bytes.NewBuffer(fileContent))

	if err != nil {
		fmt.Println("Error sending file:", err)
		return
	}
	defer resp.Body.Close()

	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Println("Error reading response body:", err)
		return
	}

	var updateResp common.Response
	if err := json.Unmarshal(body, &updateResp); err != nil {
		fmt.Println("Error unmarshalling response body:", err)
		return
	}
	if updateResp.Code != 0 {
		fmt.Printf("Create pipeline failed: %s\n", updateResp.Message)
		return
	}
}

func runUpdateCommand(cmd *cobra.Command, args []string) {

	fileContent, err := os.ReadFile(yamlFile)
	if err != nil {
		fmt.Println("Error reading YAML file:", err)
		return
	}

	resp, err := client.SendFile(http.MethodPost, fmt.Sprintf("/pipeline/update/%s", configName), bytes.NewBuffer(fileContent))

	if err != nil {
		fmt.Println("Error sending file:", err)
		return
	}
	defer resp.Body.Close()

	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Println("Error reading response body:", err)
		return
	}

	var updateResp common.Response
	if err := json.Unmarshal(body, &updateResp); err != nil {
		fmt.Println("Error unmarshalling response body:", err)
		return
	}
	if updateResp.Code != 0 {
		fmt.Printf("Update pipeline failed: %s\n", updateResp.Message)
		return
	}
}
