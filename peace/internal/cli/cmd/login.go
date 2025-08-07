package cmd

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"pace/internal/cli/client"
	"pace/internal/common"

	"github.com/spf13/cobra"
)

var (
	username string
	password string
)

// NewLoginCommand creates the login command
func NewLoginCommand() *cobra.Command {
	cmd := &cobra.Command{
		Use:   "login",
		Short: "Login to the pipeline server",
		Run:   runLogin,
	}

	cmd.Flags().StringVarP(&username, "username", "u", "", "Username for login (required)")
	cmd.Flags().StringVarP(&password, "password", "p", "", "Password for login (required)")
	cmd.MarkFlagRequired("username")
	cmd.MarkFlagRequired("password")

	return cmd
}

func runLogin(cmd *cobra.Command, args []string) {
	data := common.LoginRequest{
		Username: username,
		Password: password,
	}
	jsonData, err := json.Marshal(data)
	if err != nil {
		fmt.Printf("Error: Failed to serialize data - %v\n", err)
		return
	}

	resp, err := client.SendRequest(http.MethodPost, "/login", bytes.NewBuffer(jsonData))
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

	var loginResp common.LoginResponse
	if err := json.Unmarshal(body, &loginResp); err != nil {
		fmt.Printf("Error: Failed to parse response - %v\n", err)
		return
	}

	if loginResp.Code != 0 {
		fmt.Printf("Login failed: %s\n", loginResp.Error)
		return
	}

	client.SaveToken(loginResp.Token)
	fmt.Println("Login successful")
}
