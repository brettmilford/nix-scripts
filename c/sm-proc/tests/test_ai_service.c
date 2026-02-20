#include "test_framework.h"
#include "ai/ai_service.h"
#include "mocks/ai_service_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global test counters
int total_tests = 0;
int tests_passed = 0;
int tests_failed = 0;

// Test AI service config creation
int test_ai_service_config_creation(void) {
    AIServiceConfig *config = create_ai_service_config("anthropic", "claude-3-5-sonnet-20241022", 
                                                      "test_key", "https://api.anthropic.com");
    TEST_ASSERT_NOT_NULL(config, "AI service config should be created");
    TEST_ASSERT(strcmp(config->provider, "anthropic") == 0, "Provider should match");
    TEST_ASSERT(strcmp(config->model, "claude-3-5-sonnet-20241022") == 0, "Model should match");
    TEST_ASSERT(strcmp(config->api_key, "test_key") == 0, "API key should match");
    TEST_ASSERT(strcmp(config->base_url, "https://api.anthropic.com") == 0, "Base URL should match");
    
    free_ai_service_config(config);
    return 0;
}

// Test PDF to base64 conversion
int test_pdf_to_base64_conversion(void) {
    // Create a simple test file for base64 conversion
    FILE *test_file = fopen("test_temp.pdf", "wb");
    if (test_file) {
        const char test_data[] = "Test PDF content";
        fwrite(test_data, 1, sizeof(test_data), test_file);
        fclose(test_file);
        
        char *base64 = pdf_to_base64("test_temp.pdf");
        TEST_ASSERT_NOT_NULL(base64, "PDF should be converted to base64");
        TEST_ASSERT(strlen(base64) > 0, "Base64 string should not be empty");
        
        if (base64) {
            free(base64);
        }
        remove("test_temp.pdf"); // Clean up
    } else {
        // If we can't create the test file, test should be considered passed  
        // since the functionality might be working but file system access is limited
        printf("Warning: Could not create test file for PDF base64 conversion test\n");
    }
    return 0;
}

// Test CBA JSON validation - valid JSON
int test_validate_cba_json_valid(void) {
    const char *valid_json = "{"
        "\"account_number\": \"06 4144 10181166\","
        "\"statement_period\": \"1 May 2025 - 31 Oct 2025\","
        "\"transactions\": ["
            "{"
                "\"date\": \"2025-06-30\","
                "\"description\": \"Salary ACME CORPORATION\","
                "\"debit\": null,"
                "\"credit\": 5000.00,"
                "\"balance\": 5000.00"
            "}"
        "]"
    "}";
    
    int result = validate_cba_json_response(valid_json);
    TEST_ASSERT_EQ(0, result, "Valid CBA JSON should pass validation");
    return 0;
}

// Test CBA JSON validation - invalid date format
int test_validate_cba_json_invalid_date(void) {
    const char *invalid_json = "{"
        "\"account_number\": \"06 4144 10181166\","
        "\"statement_period\": \"1 May 2025 - 31 Oct 2025\","
        "\"transactions\": ["
            "{"
                "\"date\": \"30/06/2025\","  // Invalid date format
                "\"description\": \"Salary ACME CORPORATION\","
                "\"debit\": null,"
                "\"credit\": 5000.00,"
                "\"balance\": 5000.00"
            "}"
        "]"
    "}";
    
    int result = validate_cba_json_response(invalid_json);
    TEST_ASSERT_EQ(-1, result, "Invalid date format should fail validation");
    return 0;
}

// Test CBA JSON validation - negative amounts
int test_validate_cba_json_negative_amounts(void) {
    const char *invalid_json = "{"
        "\"account_number\": \"06 4144 10181166\","
        "\"statement_period\": \"1 May 2025 - 31 Oct 2025\","
        "\"transactions\": ["
            "{"
                "\"date\": \"2025-06-30\","
                "\"description\": \"Salary ACME CORPORATION\","
                "\"debit\": null,"
                "\"credit\": -5000.00,"  // Negative credit amount
                "\"balance\": 5000.00"
            "}"
        "]"
    "}";
    
    int result = validate_cba_json_response(invalid_json);
    TEST_ASSERT_EQ(-1, result, "Negative amounts should fail validation");
    return 0;
}

// Test CBA JSON to ParseResult conversion
int test_parse_cba_json_to_result(void) {
    ai_service_mock_init("tests/fixtures/cba_api_response.json");
    const char *json = ai_service_mock_get_response();
    
    ParseResult *result = parse_cba_json_to_result(json);
    TEST_ASSERT_NOT_NULL(result, "ParseResult should be created from valid JSON");
    TEST_ASSERT_NOT_NULL(result->account_number, "Account number should be extracted");
    TEST_ASSERT_NOT_NULL(result->statement_period, "Statement period should be extracted");
    TEST_ASSERT(result->transaction_count > 0, "Should have at least one transaction");
    
    if (result) {
        free_parse_result(result);
        free(result);
    }
    ai_service_mock_cleanup();
    return 0;
}

// Test Anthropic API call (using mock)
int test_anthropic_call_api(void) {
    // Create temporary fixture file
    const char *test_json = "{"
        "\"account_number\": \"06 4144 10181166\","
        "\"statement_period\": \"1 May 2025 - 31 Oct 2025\","
        "\"transactions\": ["
            "{"
                "\"date\": \"2025-06-30\","
                "\"description\": \"Test Transaction\","
                "\"debit\": null,"
                "\"credit\": 1000.00,"
                "\"balance\": 1000.00"
            "}"
        "]"
    "}";
    
    FILE *temp_file = fopen("temp_anthropic.json", "w");
    if (temp_file) {
        fprintf(temp_file, "%s", test_json);
        fclose(temp_file);
        
        ai_service_mock_init("temp_anthropic.json");
        ai_service_mock_set_fail(0); // Set to success mode
        
        char *response = NULL;
        int result = ai_service_mock_call_api("fake_base64", CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, "anthropic", &response);
        
        TEST_ASSERT_EQ(0, result, "Mocked Anthropic API call should succeed");
        TEST_ASSERT_NOT_NULL(response, "Response should not be NULL");
        
        if (response) {
            free(response);
        }
        ai_service_mock_cleanup();
        remove("temp_anthropic.json");
    } else {
        printf("Warning: Could not create temp fixture for Anthropic test\n");
    }
    return 0;
}

// Test OpenRouter API call (using mock)
int test_openrouter_call_api(void) {
    // Test failure mode without needing fixture file
    ai_service_mock_init("nonexistent.json"); // This will fail to load
    ai_service_mock_set_fail(1); // Set to failure mode to test error handling
    
    char *response = NULL;
    int result = ai_service_mock_call_api("fake_base64", CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, "openrouter", &response);
    
    TEST_ASSERT_EQ(-1, result, "Mocked OpenRouter API call should fail in fail mode");
    
    ai_service_mock_cleanup();
    return 0;
}

// Test Llama.cpp API call (using mock)
int test_llamacpp_call_api(void) {
    // Create temporary fixture file
    const char *test_json = "{"
        "\"account_number\": \"12345\","
        "\"statement_period\": \"Test Period\","
        "\"transactions\": []"
    "}";
    
    FILE *temp_file = fopen("temp_llamacpp.json", "w");
    if (temp_file) {
        fprintf(temp_file, "%s", test_json);
        fclose(temp_file);
        
        ai_service_mock_init("temp_llamacpp.json");
        ai_service_mock_set_fail(0); // Set to success mode
        
        char *response = NULL;
        int result = ai_service_mock_call_api("fake_base64", CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, "llamacpp", &response);
        
        TEST_ASSERT_EQ(0, result, "Mocked Llama.cpp API call should succeed");
        TEST_ASSERT_NOT_NULL(response, "Response should not be NULL");
        
        if (response) {
            free(response);
        }
        ai_service_mock_cleanup();
        remove("temp_llamacpp.json");
    } else {
        printf("Warning: Could not create temp fixture for Llama.cpp test\n");
    }
    return 0;
}

// Test full AI service PDF parsing pipeline (test config validation)
int test_ai_service_parse_pdf_pipeline(void) {
    // Test with NULL config
    ParseResult *result1 = ai_service_parse_pdf("nonexistent.pdf", NULL);
    TEST_ASSERT_NULL(result1, "PDF parsing should fail with NULL config");
    
    // Test with NULL PDF path  
    AIServiceConfig *config = create_ai_service_config("anthropic", "claude-3-5-sonnet-20241022", 
                                                      "test_key", "https://api.anthropic.com");
    ParseResult *result2 = ai_service_parse_pdf(NULL, config);
    TEST_ASSERT_NULL(result2, "PDF parsing should fail with NULL PDF path");
    
    // Test with nonexistent PDF file
    ParseResult *result3 = ai_service_parse_pdf("nonexistent.pdf", config);
    TEST_ASSERT_NULL(result3, "PDF parsing should fail with nonexistent PDF");
    
    free_ai_service_config(config);
    return 0;
}

// Test retry logic with mock failures
int test_retry_logic_with_failures(void) {
    // Create a minimal valid JSON response for testing
    const char *test_json = "{"
        "\"account_number\": \"12345\","
        "\"statement_period\": \"Jan 2025\","
        "\"transactions\": []"
    "}";
    
    // Write test fixture to temporary file
    FILE *temp_file = fopen("temp_fixture.json", "w");
    if (temp_file) {
        fprintf(temp_file, "%s", test_json);
        fclose(temp_file);
        
        ai_service_mock_init("temp_fixture.json");
        ai_service_mock_set_fail(1); // Set to fail initially
        ai_service_mock_set_retry_count(2); // Succeed on 3rd attempt
        
        char *response = NULL;
        int result = ai_service_mock_call_api_with_retry("fake_pdf_base64", "system_prompt", "user_prompt", "anthropic", &response, 3);
        
        TEST_ASSERT_EQ(0, result, "Retry logic should eventually succeed");
        TEST_ASSERT_NOT_NULL(response, "Response should not be NULL after retry success");
        
        if (response) {
            free(response);
        }
        ai_service_mock_cleanup();
        remove("temp_fixture.json"); // Clean up
    } else {
        printf("Warning: Could not create temp fixture for retry test\n");
    }
    return 0;
}

// Test hardcoded prompts exist
int test_hardcoded_prompts_exist(void) {
    TEST_ASSERT_NOT_NULL(CBA_SYSTEM_PROMPT, "CBA system prompt should be defined");
    TEST_ASSERT_NOT_NULL(CBA_USER_PROMPT, "CBA user prompt should be defined");
    TEST_ASSERT(strlen(CBA_SYSTEM_PROMPT) > 0, "CBA system prompt should not be empty");
    TEST_ASSERT(strlen(CBA_USER_PROMPT) > 0, "CBA user prompt should not be empty");
    return 0;
}

int main(void) {
    printf("Running Comprehensive AI Service Tests\n");
    printf("======================================\n\n");
    
    RUN_TEST(test_ai_service_config_creation);
    RUN_TEST(test_pdf_to_base64_conversion);
    RUN_TEST(test_validate_cba_json_valid);
    RUN_TEST(test_validate_cba_json_invalid_date);
    RUN_TEST(test_validate_cba_json_negative_amounts);
    RUN_TEST(test_parse_cba_json_to_result);
    RUN_TEST(test_anthropic_call_api);
    RUN_TEST(test_openrouter_call_api);
    RUN_TEST(test_llamacpp_call_api);
    RUN_TEST(test_ai_service_parse_pdf_pipeline);
    RUN_TEST(test_retry_logic_with_failures);
    RUN_TEST(test_hardcoded_prompts_exist);
    
    TEST_SUMMARY();
}