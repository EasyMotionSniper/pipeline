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

func NewLoginCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "login",
		Short: "Login to the pipeline server",
		Run:   runLogin,
	}

	cmd.Flags().StringP("username", "u", "", "Username for login (required)")
	cmd.Flags().StringP("password", "p", "", "Password for login (required)")
	cmd.MarkFlagRequired("username")
	cmd.MarkFlagRequired("password")

	return cmd
}

func runLogin(cmd *cobra.Command, args []string) {
	username, err := cmd.Flags().GetString("username")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	password, err := cmd.Flags().GetString("password")
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	data := api.LoginRequest{
		Username: username,
		Password: password,
	}
	jsonData, err := json.Marshal(data)
	if err != nil {
		fmt.Printf("Error: Failed to serialize data - %v\n", err)
		return
	}

	resp, err := client.SendRequest(http.MethodPost, "/login", bytes.NewBuffer(jsonData))
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		fmt.Printf("Error: Login failed - %s\n", resp.Status)
		return
	}

	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

	var loginResp common.Response
	if err := json.Unmarshal(body, &loginResp); err != nil {
		fmt.Printf("Error: Failed to parse response - %v\n", err)
		return
	}

	if loginResp.Code != 0 {
		fmt.Printf("Login failed: %s\n", loginResp.Message)
		return
	}
	token, err := common.GetAuthorizationToken(resp.Header.Get("Authorization"))
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	client.SaveToken(token)
	fmt.Printf("Login successful, token: %s\n", token)
}
