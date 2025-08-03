package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"strings"

	"pace-cli/cmd"
	"pace-cli/config"

	"github.com/spf13/cobra"
)

func main() {
	// Load configuration and existing token
	config.LoadConfig()

	// Create root command
	rootCmd := &cobra.Command{
		Run: func(cmd *cobra.Command, args []string) {
		},
	}

	// Register all commands
	cmd.RegisterCommands(rootCmd)

	// Start interactive mode
	startInteractiveMode(rootCmd)
}

func startInteractiveMode(rootCmd *cobra.Command) {
	scanner := bufio.NewScanner(os.Stdin)
	fmt.Println("Pipeline CLI - Type 'exit' to quit")
	fmt.Print(">> ")

	for scanner.Scan() {
		input := strings.TrimSpace(scanner.Text())
		if input == "exit" || input == "quit" {
			break
		}
		if input == "" {
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
