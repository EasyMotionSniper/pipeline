package cmd

import (
	"github.com/spf13/cobra"
)

// RegisterCommands adds all available commands to the root command
func RegisterCommands(rootCmd *cobra.Command) {
	rootCmd.AddCommand(NewLoginCommand())
	rootCmd.AddCommand(NewListCommand())
	rootCmd.AddCommand(NewTriggerCommand())
	rootCmd.AddCommand(NewHistoryCommand())
	rootCmd.AddCommand(NewUpdateCommand())
	rootCmd.AddCommand(NewCreateCommand())
}
