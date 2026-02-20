#include "ai_service_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Mock AI service responses
static char* mock_cba_response = NULL;
static int mock_should_fail = 0;
static int mock_retry_count = 0;
static int mock_current_attempt = 0;

// Initialize mock with fixture data
void ai_service_mock_init(const char *fixture_json_path) {
    if (mock_cba_response) {
        free(mock_cba_response);
        mock_cba_response = NULL;
    }
    
    FILE *file = fopen(fixture_json_path, "r");
    if (!file) {
        // Only print error if not in test mode (suppress expected failures)
        if (!getenv("TEST_MODE")) {
            fprintf(stderr, "Failed to open fixture file: %s\n", fixture_json_path);
        }
        return;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    mock_cba_response = malloc(file_size + 1);
    if (mock_cba_response) {
        fread(mock_cba_response, 1, file_size, file);
        mock_cba_response[file_size] = '\0';
    }
    
    fclose(file);
}

// Set mock to fail mode
void ai_service_mock_set_fail(int should_fail) {
    mock_should_fail = should_fail;
}

// Set retry count for testing retry logic
void ai_service_mock_set_retry_count(int retry_count) {
    mock_retry_count = retry_count;
    mock_current_attempt = 0;
}

// Mock API call function
int ai_service_mock_call_api(const char *pdf_base64, const char *system_prompt, 
                           const char *user_prompt, const char *provider, char **response) {
    // Unused parameters
    (void)pdf_base64;
    (void)system_prompt;
    (void)user_prompt;
    (void)provider;
    
    if (mock_should_fail) {
        return -1; // Simulate API failure
    }
    
    if (!mock_cba_response) {
        return -1; // No mock data loaded
    }
    
    // Return a copy of the mock response
    *response = malloc(strlen(mock_cba_response) + 1);
    if (*response) {
        strcpy(*response, mock_cba_response);
        return 0; // Success
    }
    
    return -1; // Memory allocation failed
}

// Cleanup mock
void ai_service_mock_cleanup(void) {
    if (mock_cba_response) {
        free(mock_cba_response);
        mock_cba_response = NULL;
    }
}

// Get current mock response for testing
const char* ai_service_mock_get_response(void) {
    return mock_cba_response;
}

// Mock API call with retry logic for testing
int ai_service_mock_call_api_with_retry(const char *pdf_base64, const char *system_prompt, 
                                       const char *user_prompt, const char *provider, 
                                       char **response, int max_retries) {
    int result = -1;
    
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        mock_current_attempt = attempt;
        
        // If we're supposed to fail but this is the retry count where we succeed
        if (mock_should_fail && attempt < mock_retry_count) {
            usleep(100000); // 100ms delay between retries
            continue;
        }
        
        // Temporarily disable fail mode if we've reached the success attempt
        int original_fail_state = mock_should_fail;
        if (mock_should_fail && attempt >= mock_retry_count) {
            mock_should_fail = 0; // Allow success
        }
        
        // Attempt the API call
        result = ai_service_mock_call_api(pdf_base64, system_prompt, user_prompt, provider, response);
        
        // Restore original fail state
        mock_should_fail = original_fail_state;
        
        if (result == 0) {
            break; // Success
        }
        
        // Add delay between retries (exponential backoff simulation)
        if (attempt < max_retries) {
            usleep(100000 * (attempt + 1));
        }
    }
    
    return result;
}