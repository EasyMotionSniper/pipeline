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

type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

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
	data := LoginRequest{
		Username: username,
		Password: password,
	}
	jsonData, err := json.Marshal(data)
	if err != nil {
		fmt.Printf("Error: Failed to serialize data - %v\n", err)
		return
	}

	url := fmt.Sprintf("%s/login", config.ServerURL)
	resp, err := http.Post(url, "application/json", bytes.NewBuffer(jsonData))
	if err != nil {
		fmt.Printf("Error: Could not connect to server - %v\n", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		fmt.Printf("Error: Login failed - %s\n", resp.Status)
		return
	}

	// Parse response
	body, err := client.ReadResponseBody(resp)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}

	var loginResp client.LoginResponse
	if err := json.Unmarshal(body, &loginResp); err != nil {
		fmt.Printf("Error: Failed to parse response - %v\n", err)
		return
	}

	if loginResp.Error != "" {
		fmt.Printf("Login failed: %s\n", loginResp.Error)
		return
	}

	fmt.Println("Login successful")
}
