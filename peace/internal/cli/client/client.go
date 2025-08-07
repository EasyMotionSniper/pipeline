package client

import (
	"fmt"
	"io"
	"net/http"
)

var token string
var serverURL = "http://localhost:8080"

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

func SaveToken(t string) {
	token = t
}

func SendRequest(method, path string, body io.Reader) (*http.Response, error) {
	req, err := CreateRequest(method, path, body)
	if err != nil {
		return nil, err
	}
	return DoRequest(req)
}

func SendFile(method, path string, file io.Reader) (*http.Response, error) {
	req, err := CreateRequest(method, path, file)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/yaml")
	return DoRequest(req)
}

func CreateRequest(method, path string, body io.Reader) (*http.Request, error) {
	url := serverURL + path
	req, err := http.NewRequest(method, url, body)
	fmt.Println(url)
	if err != nil {
		return nil, err
	}

	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	return req, nil
}

func DoRequest(req *http.Request) (*http.Response, error) {
	client := &http.Client{}
	return client.Do(req)
}

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
