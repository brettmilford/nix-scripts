#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test framework macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", message, __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "ASSERTION FAILED: %s - Expected: %d, Actual: %d at %s:%d\n", \
                   message, (int)(expected), (int)(actual), __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_STR_EQ(expected, actual, message) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "ASSERTION FAILED: %s - Expected: '%s', Actual: '%s' at %s:%d\n", \
                   message, (expected), (actual), __FILE__, __LINE__); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr, message) \
    TEST_ASSERT((ptr) == NULL, message)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    TEST_ASSERT((ptr) != NULL, message)

// Test runner macros
#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        fflush(stdout); \
        int result = test_func(); \
        if (result == 0) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
            tests_failed++; \
        } \
        total_tests++; \
    } while(0)

#define TEST_SUMMARY() \
    do { \
        printf("\n=== Test Summary ===\n"); \
        printf("Total tests: %d\n", total_tests); \
        printf("Passed: %d\n", tests_passed); \
        printf("Failed: %d\n", tests_failed); \
        if (tests_failed == 0) { \
            printf("All tests passed!\n"); \
            return 0; \
        } else { \
            printf("Some tests failed.\n"); \
            return 1; \
        } \
    } while(0)

// Global test counters (declare these in your test files)
extern int total_tests;
extern int tests_passed;
extern int tests_failed;

#endif // TEST_FRAMEWORK_H
