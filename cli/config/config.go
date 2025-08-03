package config

import (
	"os"
	"path/filepath"
)

var (
	// ServerURL is the base URL of the pipeline server
	ServerURL = "http://localhost:8080"
	
	// token stores the current authentication token
	token string
)

// LoadConfig loads configuration from file
func LoadConfig() {
	// Load token from file if it exists
	data, err := os.ReadFile(getConfigFilePath())
	if err == nil {
		token = string(data)
	}
}

// SaveConfig saves configuration to file
func SaveConfig() {
	if token != "" {
		err := os.WriteFile(getConfigFilePath(), []byte(token), 0600)
		if err != nil {
			// We just warn here since it's non-critical
			// fmt.Printf("Warning: Could not save configuration - %v\n", err)
		}
	}
}

// GetToken returns the current authentication token
func GetToken() string {
	return token
}

// SetToken sets the authentication token
func SetToken(newToken string) {
	token = newToken
}

// getConfigFilePath returns the path to the configuration file
func getConfigFilePath() string {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		return ".pipeline_token"
	}
	return filepath.Join(homeDir, ".pipeline_token")
}
    