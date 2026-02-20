#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

// Hardcoded configuration
#define TAG_ACCOUNTS_ID 123        // "Accounts" tag
#define TAG_PROCESSED_ID 456       // "processed" tag
#define FIELD_EXPENSE_REPORT_ID 789 // "Expense Report" field

// API configuration
#define API_TIMEOUT_SECONDS 30
#define API_MAX_RETRIES 3
#define API_RETRY_BACKOFF_MS 1000  // Initial backoff, doubles each retry

// Date format
#define DATE_FORMAT_ISO "%Y-%m-%d"

typedef struct {
    char *pattern;    // Regex pattern string
    char *category;   // Category name
    pcre2_code *compiled_regex;  // Compiled regex
} CategoryRule;

typedef struct {
    char *api_key_env;   // Environment variable name for API key
    char *base_url;      // API endpoint base URL
    char *model;         // Model name
} AIProviderConfig;

typedef struct {
    char *method;        // "content" or "ai"
    char *provider;      // AI provider name (if method = "ai")
} ParserConfig;

typedef struct {
    char *default_category;
    CategoryRule *rules;
    size_t rule_count;
    
    // Parser configurations
    ParserConfig *anz_config;
    ParserConfig *cba_config;
    
    // AI provider configurations
    AIProviderConfig *anthropic_config;
    AIProviderConfig *openrouter_config;
    AIProviderConfig *llamacpp_config;
} Config;

// Configuration functions
Config* load_config(const char *config_file);
void free_config(Config *cfg);

#endif // CONFIG_H
