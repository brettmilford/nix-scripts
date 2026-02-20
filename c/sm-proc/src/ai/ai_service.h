#ifndef AI_SERVICE_H
#define AI_SERVICE_H

#include "../parsers/parser.h"

typedef struct {
    char *provider;     // "anthropic", "openrouter", "llamacpp"
    char *model;        // "claude-3-5-sonnet-20241022"
    char *api_key;      // From env var
    char *base_url;     // Provider endpoint
} AIServiceConfig;

// Main AI service function
ParseResult* ai_service_parse_pdf(const char *pdf_path, AIServiceConfig *config);

// Provider-specific implementations
int anthropic_call_api(const char *pdf_base64, const char *system_prompt, 
                       const char *user_prompt, AIServiceConfig *config, char **response);
int openrouter_call_api(const char *pdf_base64, const char *system_prompt, 
                        const char *user_prompt, AIServiceConfig *config, char **response);
int llamacpp_call_api(const char *pdf_base64, const char *system_prompt, 
                      const char *user_prompt, AIServiceConfig *config, char **response);

// JSON validation and parsing
int validate_cba_json_response(const char *json_str);
ParseResult* parse_cba_json_to_result(const char *json_str);

// PDF utilities
char* pdf_to_base64(const char *pdf_path);

// Configuration utilities
AIServiceConfig* create_ai_service_config(const char *provider, const char *model, 
                                         const char *api_key, const char *base_url);
void free_ai_service_config(AIServiceConfig *config);

// Hardcoded prompts for CBA
extern const char *CBA_SYSTEM_PROMPT;
extern const char *CBA_USER_PROMPT;

#endif // AI_SERVICE_H
