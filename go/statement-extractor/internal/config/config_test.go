package config

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestLoadConfig(t *testing.T) {
	// Create a temporary config file
	configContent := `
default_category = "Test Category"

[parsers]
  [parsers.test_bank]
  method = "pdf"
  provider = "test-service"

[pdf_services]
  [pdf_services.test-service]
  api_key_env = "TEST_API_KEY"
  base_url = "https://test.example.com"
  model = "test-model"

[[categories]]
pattern = "TEST.*PATTERN"
category = "Test"
`

	tmpDir := t.TempDir()
	configPath := filepath.Join(tmpDir, "test-config.toml")
	
	err := os.WriteFile(configPath, []byte(configContent), 0644)
	require.NoError(t, err)
	
	// Load the config
	config, err := LoadConfig(configPath)
	require.NoError(t, err)
	
	// Verify config values
	assert.Equal(t, "Test Category", config.DefaultCategory)
	
	// Check parser config
	assert.Contains(t, config.Parsers, "test_bank")
	parser := config.Parsers["test_bank"]
	assert.Equal(t, "pdf", parser.Method)
	assert.Equal(t, "test-service", parser.Provider)
	
	// Check PDF service config
	assert.Contains(t, config.PDFServices, "test-service")
	service := config.PDFServices["test-service"]
	assert.Equal(t, "TEST_API_KEY", service.APIKeyEnv)
	assert.Equal(t, "https://test.example.com", service.BaseURL)
	assert.Equal(t, "test-model", service.Model)
	
	// Check categories
	assert.Len(t, config.Categories, 1)
	assert.Equal(t, "TEST.*PATTERN", config.Categories[0].Pattern)
	assert.Equal(t, "Test", config.Categories[0].Category)
}

func TestLoadConfig_InvalidFile(t *testing.T) {
	config, err := LoadConfig("nonexistent.toml")
	assert.Error(t, err)
	assert.Nil(t, config)
	assert.Contains(t, err.Error(), "failed to read config file")
}