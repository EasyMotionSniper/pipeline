package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"time"
)

// LoginRequest 登录请求结构
type LoginRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

// LoginResponse 登录响应结构
type LoginResponse struct {
	Message string `json:"message"`
	Token   string `json:"token"`
}

func main() {
	client := &http.Client{
		Timeout: 10 * time.Second,
	}

	// 1. 登录获取令牌
	var token string
	{
		loginURL := "http://localhost:8080/login"
		reqBody := LoginRequest{
			Username: "alice",
			Password: "password123",
		}

		jsonData, err := json.Marshal(reqBody)
		if err != nil {
			fmt.Printf("JSON序列化失败: %v\n", err)
			return
		}

		resp, err := client.Post(loginURL, "application/json", bytes.NewBuffer(jsonData))
		if err != nil {
			fmt.Printf("登录请求失败: %v\n", err)
			return
		}
		defer resp.Body.Close()

		body, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			fmt.Printf("读取响应失败: %v\n", err)
			return
		}

		if resp.StatusCode != http.StatusOK {
			fmt.Printf("登录失败: %s\n", string(body))
			return
		}

		var loginResp LoginResponse
		if err := json.Unmarshal(body, &loginResp); err != nil {
			fmt.Printf("解析登录响应失败: %v\n", err)
			return
		}

		token = loginResp.Token
		fmt.Printf("登录成功，获取令牌: %s\n", token)
	}

	// 2. 多次访问受保护资源，演示令牌刷新
	for i := 0; i < 6; i++ {
		fmt.Printf("\n第 %d 次访问受保护资源\n", i+1)

		// 访问/user端点
		{
			req, err := http.NewRequest("GET", "http://localhost:8080/api/user", nil)
			if err != nil {
				fmt.Printf("创建请求失败: %v\n", err)
				return
			}

			// 添加Authorization头
			req.Header.Set("Authorization", "Bearer "+token)

			resp, err := client.Do(req)
			if err != nil {
				fmt.Printf("请求失败: %v\n", err)
				return
			}
			defer resp.Body.Close()

			body, err := ioutil.ReadAll(resp.Body)
			if err != nil {
				fmt.Printf("读取响应失败: %v\n", err)
				return
			}

			if resp.StatusCode != http.StatusOK {
				fmt.Printf("访问失败: %s\n", string(body))
				return
			}

			// 检查是否有新令牌
			newToken := resp.Header.Get("X-Refreshed-Token")
			if newToken != "" && newToken != token {
				fmt.Printf("令牌已刷新: %s\n", newToken)
				token = newToken
			}

			fmt.Printf("访问/user响应: %s\n", string(body))
		}

		// 等待一段时间，让令牌接近过期
		time.Sleep(1 * time.Minute)
	}
}
