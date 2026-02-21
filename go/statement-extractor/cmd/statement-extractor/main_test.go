package main

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestMainFunction(t *testing.T) {
	// Test that rootCmd is defined and has expected properties
	assert.NotNil(t, rootCmd, "rootCmd should be defined")
	assert.Equal(t, "statement-extractor", rootCmd.Use)
	assert.Contains(t, rootCmd.Short, "Extract and categorize")
	assert.Contains(t, rootCmd.Long, "Statement Extractor")
}