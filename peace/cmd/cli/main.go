package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"pace/internal/cli/cmd"
	"pace/internal/common"
	"strings"

	"github.com/spf13/cobra"
)

var rootStartCmd = &cobra.Command{
	Use:   "main",
	Short: "Pipeline CLI with startup parameters",
	Run: func(cmd *cobra.Command, args []string) {
		ip, err := cmd.Flags().GetString("ip")
		if ip == "localhost" {
			ip = "127.0.0.1"
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "get ip failed: %v\n", err)
			os.Exit(1)
		}

		if !common.IsValidIP(ip) {
			fmt.Fprintf(os.Stderr, "invalid ip %s\n", ip)
			os.Exit(1)
		}
		port, err := cmd.Flags().GetInt("port")
		if err != nil {
			fmt.Fprintf(os.Stderr, "get port failed: %v\n", err)
			os.Exit(1)
		}

		if !common.IsValidPort(port) {
			fmt.Fprintf(os.Stderr, "invalid port %d\n", port)
			os.Exit(1)
		}

		if !common.IsServerOnline(ip, port) {
			fmt.Fprintf(os.Stderr, "server %s:%d not online\n", ip, port)
			os.Exit(1)
		}

	},
}

func init() {
	rootStartCmd.Flags().StringP("ip", "i", "127.0.0.1", "Target IP address (e.g., -i 127.0.0.1)")
	rootStartCmd.Flags().IntP("port", "p", 8080, "Target port (e.g., -p 8080)")
}

func main() {
	if err := rootStartCmd.Execute(); err != nil {
		fmt.Fprintf(os.Stderr, "启动失败: %v\n", err)
		os.Exit(1)
	}
	startInteractiveMode()
}

func startInteractiveMode() {
	scanner := bufio.NewScanner(os.Stdin)
	fmt.Println("Pipeline CLI - Type 'help' to show help, 'exit' to exit")
	fmt.Print(">> ")

	for scanner.Scan() {
		rootCmd := &cobra.Command{
			Run: func(cmd *cobra.Command, args []string) {
			},
		}

		cmd.RegisterCommands(rootCmd)
		input := strings.TrimSpace(scanner.Text())
		if input == "exit" {
			break
		}
		if input == "" {
			fmt.Print(">> ")
			continue
		}

		if input == "help" {
			rootCmd.Help()
			fmt.Print(">> ")
			continue
		}

		args := strings.Fields(input)
		cmd, _, err := rootCmd.Find(args)
		if err != nil || cmd == nil {
			if err := executeShellCommand(args[0], args[1:]); err != nil {
				fmt.Printf("Error: %v\n", err)
			}
			fmt.Print(">> ")
			continue
		}
		rootCmd.SetArgs(args)
		if err := rootCmd.Execute(); err != nil {
			fmt.Printf("Error: %v\n", err)
		}
		fmt.Print(">> ")
	}
}

func executeShellCommand(cmdName string, cmdArgs []string) error {
	cmd := exec.Command(cmdName, cmdArgs...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
