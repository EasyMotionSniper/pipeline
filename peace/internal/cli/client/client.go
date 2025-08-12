package client

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io"
	"net/http"
	"os"
)

var (
	token      string
	serverURL  = "https://localhost:8080"
	caCertPath = "/home/ubuntu/pipeline/peace/script/server.crt"
)

func init() {
	if envCaPath := os.Getenv("CA_CERT_PATH"); envCaPath != "" {
		caCertPath = envCaPath
	}
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
	if err != nil {
		return nil, err
	}

	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	req.Header.Set("Accept", "application/json")
	return req, nil
}

func DoRequest(req *http.Request) (*http.Response, error) {
	client := &http.Client{
		Transport: createTLSConfig(),
	}
	return client.Do(req)
}

func createTLSConfig() *http.Transport {
	tlsConfig := &tls.Config{}

	// tlsConfig.InsecureSkipVerify = true

	if caCertPath != "" {
		caCert, err := os.ReadFile(caCertPath)
		if err != nil {
			fmt.Printf("fail to read ca cert: %v\n", err)
		} else {
			caCertPool := x509.NewCertPool()
			if caCertPool.AppendCertsFromPEM(caCert) {
				tlsConfig.RootCAs = caCertPool
			} else {
				fmt.Println("fail to parse ca cert, use system default cert pool")
			}
		}
	}
	return &http.Transport{
		TLSClientConfig: tlsConfig,
	}
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
