package client

import (
	"fmt"
	"io"
	"net/http"
	"pace-cli/config"
)

// LoginResponse represents the authentication response
type LoginResponse struct {
	Token string `json:"token"`
	Error string `json:"error"`
}

// Pipeline represents a pipeline entity
type Pipeline struct {
	ID     string `json:"id"`
	Name   string `json:"name"`
	Config string `json:"config"`
}

// ExecutionHistory represents a pipeline execution record
type ExecutionHistory struct {
	ID         string `json:"id"`
	PipelineID string `json:"pipeline_id"`
	Status     string `json:"status"`
	StartTime  string `json:"start_time"`
	EndTime    string `json:"end_time"`
}

// CreateAuthenticatedRequest creates a new HTTP request with authentication header
func CreateAuthenticatedRequest(method, url string, body io.Reader) (*http.Request, error) {
	req, err := http.NewRequest(method, url, body)
	if err != nil {
		return nil, err
	}

	// Add auth header if token exists
	if config.GetToken() != "" {
		req.Header.Set("Authorization", "Bearer "+config.GetToken())
	}

	return req, nil
}

// DoRequest executes an HTTP request
func DoRequest(req *http.Request) (*http.Response, error) {
	client := &http.Client{}
	return client.Do(req)
}

// ReadResponseBody reads and returns the response body
func ReadResponseBody(resp *http.Response) ([]byte, error) {
	if resp == nil {
		return nil, fmt.Errorf("response is nil")
	}
	if resp.Body == nil {
		return nil, fmt.Errorf("response body is nil")
	}
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body failed: %w", err)
	}
	return body, nil
}
