#include "test_framework.h"
#include "parsers/cba_parser.h"
#include "parsers/anz_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global test counters
int total_tests = 0;
int tests_passed = 0;
int tests_failed = 0;

// Test CBA parser with NULL input
int test_cba_parser_null_input(void) {
    ParseResult *result = parse_cba_statement(NULL, "133");
    TEST_ASSERT_NULL(result, "CBA parser should return NULL for NULL content");
    return 0;
}

// Test CBA parser with empty input
int test_cba_parser_empty_input(void) {
    ParseResult *result = parse_cba_statement("", "133");
    TEST_ASSERT_NOT_NULL(result, "CBA parser should handle empty content");
    TEST_ASSERT_EQ(0, result->transaction_count, "Empty content should result in 0 transactions");
    
    if (result) {
        free_parse_result(result);
        free(result);
    }
    return 0;
}

// Test ANZ parser with NULL input
int test_anz_parser_null_input(void) {
    ParseResult *result = parse_anz_statement(NULL, "11");
    TEST_ASSERT_NULL(result, "ANZ parser should return NULL for NULL content");
    return 0;
}

// Test ANZ parser with empty input
int test_anz_parser_empty_input(void) {
    ParseResult *result = parse_anz_statement("", "11");
    TEST_ASSERT_NOT_NULL(result, "ANZ parser should handle empty content");
    TEST_ASSERT_EQ(0, result->transaction_count, "Empty content should result in 0 transactions");
    
    if (result) {
        free_parse_result(result);
        free(result);
    }
    return 0;
}

// Test basic transaction structure
int test_transaction_structure(void) {
    // Test with minimal valid CBA statement content
    const char *sample_content = 
        "Account Number: 06 4144 10181166\n"
        "Statement Period: 1 May 2025 - 31 Oct 2025\n"
        "Transaction Details\n"
        "30 May Salary ACME CORPORATION $5,000.00 $5,000.00 CR\n";
    
    ParseResult *result = parse_cba_statement(sample_content, "133");
    TEST_ASSERT_NOT_NULL(result, "CBA parser should handle sample content");
    
    if (result) {
        TEST_ASSERT_NOT_NULL(result->account_number, "Account number should be extracted");
        TEST_ASSERT_NOT_NULL(result->statement_period, "Statement period should be extracted");
        
        if (result->account_number) {
            TEST_ASSERT(strstr(result->account_number, "06 4144 10181166") != NULL, 
                       "Account number should match expected value");
        }
        
        if (result->statement_period) {
            TEST_ASSERT(strstr(result->statement_period, "1 May 2025") != NULL,
                       "Statement period should contain start date");
        }
        
        free_parse_result(result);
        free(result);
    }
    return 0;
}

int main(void) {
    printf("Running Parser Tests\n");
    printf("===================\n\n");
    
    RUN_TEST(test_cba_parser_null_input);
    RUN_TEST(test_cba_parser_empty_input);
    RUN_TEST(test_anz_parser_null_input);
    RUN_TEST(test_anz_parser_empty_input);
    RUN_TEST(test_transaction_structure);
    
    TEST_SUMMARY();
}