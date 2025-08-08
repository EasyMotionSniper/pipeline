package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"
)

const (
	apiBaseURL = "http://localhost:8080/api"
)

func main() {
	// 定义命令
	createCmd := flag.NewFlagSet("create", flag.ExitOnError)
	updateCmd := flag.NewFlagSet("update", flag.ExitOnError)
	getCmd := flag.NewFlagSet("get", flag.ExitOnError)
	triggerCmd := flag.NewFlagSet("trigger", flag.ExitOnError)

	// 命令参数
	createFile := createCmd.String("file", "", "YAML配置文件路径 (必填)")
	updateFile := updateCmd.String("file", "", "YAML配置文件路径 (必填)")
	getName := getCmd.String("name", "", "流水线名称 (必填)")
	getVersion := getCmd.String("version", "", "流水线版本 (可选)")
	triggerName := triggerCmd.String("name", "", "流水线名称 (必填)")

	if len(os.Args) < 2 {
		fmt.Println("请指定命令: create, update, get, trigger")
		os.Exit(1)
	}

	// 解析命令
	switch os.Args[1] {
	case "create":
		createCmd.Parse(os.Args[2:])
		if *createFile == "" {
			createCmd.Usage()
			os.Exit(1)
		}
		createPipeline(*createFile)

	case "update":
		updateCmd.Parse(os.Args[2:])
		if *updateFile == "" {
			updateCmd.Usage()
			os.Exit(1)
		}
		updatePipeline(*updateFile)

	case "get":
		getCmd.Parse(os.Args[2:])
		if *getName == "" {
			getCmd.Usage()
			os.Exit(1)
		}
		getPipeline(*getName, *getVersion)

	case "trigger":
		triggerCmd.Parse(os.Args[2:])
		if *triggerName == "" {
			triggerCmd.Usage()
			os.Exit(1)
		}
		triggerPipeline(*triggerName)

	default:
		fmt.Println("未知命令: ", os.Args[1])
		fmt.Println("可用命令: create, update, get, trigger")
		os.Exit(1)
	}
}

// 创建流水线
func createPipeline(filePath string) {
	err := uploadPipelineFile(filePath, "POST")
	if err != nil {
		fmt.Printf("创建流水线失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("流水线创建成功")
}

// 更新流水线
func updatePipeline(filePath string) {
	err := uploadPipelineFile(filePath, "POST") // 与create使用相同的API端点
	if err != nil {
		fmt.Printf("更新流水线失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("流水线更新成功")
}

// 上传流水线配置文件
func uploadPipelineFile(filePath string, method string) error {
	file, err := os.Open(filePath)
	if err != nil {
		return fmt.Errorf("无法打开文件: %v", err)
	}
	defer file.Close()

	// 创建表单数据
	body := &bytes.Buffer{}
	writer := multipart.NewWriter(body)
	part, err := writer.CreateFormFile("file", filepath.Base(filePath))
	if err != nil {
		return fmt.Errorf("创建表单失败: %v", err)
	}

	_, err = io.Copy(part, file)
	if err != nil {
		return fmt.Errorf("复制文件内容失败: %v", err)
	}

	writer.Close()

	// 发送请求
	req, err := http.NewRequest(method, apiBaseURL+"/pipelines", body)
	if err != nil {
		return fmt.Errorf("创建请求失败: %v", err)
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("发送请求失败: %v", err)
	}
	defer resp.Body.Close()

	// 读取响应
	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %v", err)
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("请求失败 (状态码: %d): %s", resp.StatusCode, string(respBody))
	}

	fmt.Println("响应:", string(respBody))
	return nil
}

// 获取流水线信息
func getPipeline(name, version string) {
	url := fmt.Sprintf("%s/pipelines/%s", apiBaseURL, name)
	if version != "" {
		url += "?version=" + version
	}

	resp, err := http.Get(url)
	if err != nil {
		fmt.Printf("请求失败: %v\n", err)
		os.Exit(1)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf("读取响应失败: %v\n", err)
		os.Exit(1)
	}

	if resp.StatusCode != http.StatusOK {
		fmt.Printf("获取流水线失败 (状态码: %d): %s\n", resp.StatusCode, string(body))
		os.Exit(1)
	}

	fmt.Println("流水线信息:")
	fmt.Println(string(body))
}

// 触发流水线执行
func triggerPipeline(name string) {
	url := fmt.Sprintf("%s/pipelines/%s/trigger", apiBaseURL, name)

	resp, err := http.Post(url, "application/json", nil)
	if err != nil {
		fmt.Printf("请求失败: %v\n", err)
		os.Exit(1)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf("读取响应失败: %v\n", err)
		os.Exit(1)
	}

	if resp.StatusCode != http.StatusOK {
		fmt.Printf("触发流水线失败 (状态码: %d): %s\n", resp.StatusCode, string(body))
		os.Exit(1)
	}

	fmt.Println("响应:", string(body))
}
