#ifndef AI_SERVICE_MOCK_H
#define AI_SERVICE_MOCK_H

// Initialize mock with fixture data
void ai_service_mock_init(const char *fixture_json_path);

// Set mock to fail mode for testing error handling
void ai_service_mock_set_fail(int should_fail);

// Set retry count for testing retry logic
void ai_service_mock_set_retry_count(int retry_count);

// Mock API call function - mimics ai_service API
int ai_service_mock_call_api(const char *pdf_base64, const char *system_prompt, 
                           const char *user_prompt, const char *provider, char **response);

// Mock API call with retry logic for testing
int ai_service_mock_call_api_with_retry(const char *pdf_base64, const char *system_prompt, 
                                       const char *user_prompt, const char *provider, 
                                       char **response, int max_retries);

// Cleanup mock resources
void ai_service_mock_cleanup(void);

// Get current mock response for testing
const char* ai_service_mock_get_response(void);

#endif // AI_SERVICE_MOCK_H
