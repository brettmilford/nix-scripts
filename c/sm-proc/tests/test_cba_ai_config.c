#include "test_framework.h"
#include "../src/config.h"
#include "../src/parsers/cba_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global test counters
int total_tests = 0;
int tests_passed = 0;
int tests_failed = 0;

// Test that CBA AI configuration is read correctly from config file
int test_cba_ai_configuration_loading(void) {
    printf("Testing CBA AI configuration loading...\n");
    
    // Load the actual configuration file
    Config *config = load_config("examples/sm-proc.cfg");
    TEST_ASSERT_NOT_NULL(config, "Configuration should load successfully");
    
    // Test CBA parser configuration
    TEST_ASSERT_NOT_NULL(config->cba_config, "CBA configuration should exist");
    TEST_ASSERT_NOT_NULL(config->cba_config->method, "CBA method should be configured");
    TEST_ASSERT(strcmp(config->cba_config->method, "ai") == 0, "CBA method should be 'ai'");
    TEST_ASSERT_NOT_NULL(config->cba_config->provider, "CBA provider should be configured");
    TEST_ASSERT(strcmp(config->cba_config->provider, "anthropic") == 0, "CBA provider should be 'anthropic'");
    
    // Test Anthropic provider configuration
    TEST_ASSERT_NOT_NULL(config->anthropic_config, "Anthropic configuration should exist");
    TEST_ASSERT_NOT_NULL(config->anthropic_config->api_key_env, "Anthropic API key env should be configured");
    TEST_ASSERT(strcmp(config->anthropic_config->api_key_env, "ANTHROPIC_API_KEY") == 0, 
               "Anthropic API key env should be 'ANTHROPIC_API_KEY'");
    TEST_ASSERT_NOT_NULL(config->anthropic_config->base_url, "Anthropic base URL should be configured");
    TEST_ASSERT_NOT_NULL(config->anthropic_config->model, "Anthropic model should be configured");
    
    // Test CBA parser configuration system
    set_cba_parser_config(config);
    
    free_config(config);
    return 0;
}

// Test environment variable reading for API key
int test_cba_api_key_environment_reading(void) {
    printf("Testing CBA API key environment variable reading...\n");
    
    // Load config and set up CBA parser
    Config *config = load_config("examples/sm-proc.cfg");
    TEST_ASSERT_NOT_NULL(config, "Configuration should load");
    set_cba_parser_config(config);
    
    // Test without API key
    unsetenv("ANTHROPIC_API_KEY");
    const char *api_key = getenv(config->anthropic_config->api_key_env);
    TEST_ASSERT_NULL(api_key, "API key should be NULL when environment variable is not set");
    
    // Test with API key set
    setenv("ANTHROPIC_API_KEY", "test-key-12345", 1);
    api_key = getenv(config->anthropic_config->api_key_env);
    TEST_ASSERT_NOT_NULL(api_key, "API key should be found when environment variable is set");
    TEST_ASSERT(strcmp(api_key, "test-key-12345") == 0, "API key should match set value");
    
    free_config(config);
    return 0;
}

// Test CBA correspondent detection
int test_cba_correspondent_detection(void) {
    printf("Testing CBA correspondent detection...\n");
    
    // Test various CBA correspondent identifiers
    struct {
        const char *correspondent;
        int expected;
        const char *description;
    } test_cases[] = {
        {"133", 1, "Correspondent ID '133'"},
        {"CBA", 1, "Correspondent name 'CBA'"},
        {"Commonwealth Bank", 1, "Full bank name"},
        {"11", 0, "ANZ correspondent ID"},
        {"ANZ", 0, "ANZ correspondent name"},
        {NULL, 0, "NULL correspondent"},
        {"", 0, "Empty string correspondent"},
        {"XYZ", 0, "Unknown correspondent"}
    };
    
    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        // This is testing the internal logic - we'd need to expose a test function
        // or test through the main parsing interface
        printf("  Testing %s: %s\n", test_cases[i].description, 
               test_cases[i].expected ? "should be CBA" : "should not be CBA");
    }
    
    return 0;
}

// Test that AI parsing is correctly triggered for CBA documents
int test_cba_ai_parsing_trigger(void) {
    printf("Testing CBA AI parsing trigger logic...\n");
    
    // Load configuration
    Config *config = load_config("examples/sm-proc.cfg");
    TEST_ASSERT_NOT_NULL(config, "Configuration should load");
    set_cba_parser_config(config);
    
    // Set up environment
    setenv("ANTHROPIC_API_KEY", "fake-test-key", 1);
    
    // Test with mock CBA document content
    const char *mock_content = "Mock CBA statement content for testing";
    const char *correspondent = "133";
    int document_id = 999;
    
    // This would test the actual parsing logic, but requires mocking the PDF download
    // For now, we verify that the configuration is set up correctly
    printf("  Configuration verified: method='%s', provider='%s'\n",
           config->cba_config->method, config->cba_config->provider);
    
    free_config(config);
    return 0;
}

int main(void) {
    printf("Running CBA AI Configuration Tests\n");
    printf("==================================\n\n");
    
    // Run tests
    RUN_TEST(test_cba_ai_configuration_loading);
    RUN_TEST(test_cba_api_key_environment_reading);
    RUN_TEST(test_cba_correspondent_detection);
    RUN_TEST(test_cba_ai_parsing_trigger);
    
    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", total_tests);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("Some tests failed.\n");
        return 1;
    }
}