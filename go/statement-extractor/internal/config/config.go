package config

import (
	"fmt"

	"github.com/spf13/viper"
)

// Config represents the application configuration
type Config struct {
	DefaultCategory string                    `mapstructure:"default_category"`
	Parsers         map[string]ParserConfig   `mapstructure:"parsers"`
	PDFServices     map[string]ServiceConfig  `mapstructure:"pdf_services"`
	Categories      []CategoryRule            `mapstructure:"categories"`
}

// ParserConfig defines how to parse different bank statements
type ParserConfig struct {
	Method   string `mapstructure:"method"`   // "content" or "pdf"
	Provider string `mapstructure:"provider"` // PDF service provider name
}

// ServiceConfig defines PDF service provider settings
type ServiceConfig struct {
	APIKeyEnv string `mapstructure:"api_key_env"`
	BaseURL   string `mapstructure:"base_url"`
	Model     string `mapstructure:"model"`
}

// CategoryRule defines a transaction categorization rule
type CategoryRule struct {
	Pattern  string `mapstructure:"pattern"`
	Category string `mapstructure:"category"`
}

// LoadConfig loads configuration from file and environment variables
func LoadConfig(configPath string) (*Config, error) {
	viper.SetConfigFile(configPath)
	viper.SetConfigType("toml")
	
	// Set defaults
	viper.SetDefault("default_category", "Uncategorized")
	
	if err := viper.ReadInConfig(); err != nil {
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}
	
	var config Config
	if err := viper.Unmarshal(&config); err != nil {
		return nil, fmt.Errorf("failed to unmarshal config: %w", err)
	}
	
	return &config, nil
}