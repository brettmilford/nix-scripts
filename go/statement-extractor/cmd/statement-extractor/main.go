package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

func main() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

var rootCmd = &cobra.Command{
	Use:   "statement-extractor",
	Short: "Extract and categorize transactions from bank statements",
	Long: `Statement Extractor is a tool for processing PDF bank statements,
extracting transaction data, and categorizing transactions based on configurable rules.`,
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Statement Extractor v1.0.0")
		fmt.Println("Use --help for available commands")
	},
}