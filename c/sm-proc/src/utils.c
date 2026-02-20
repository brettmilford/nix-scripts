#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

void log_message(LogLevel level, const char *format, ...) {
    const char *level_str;
    FILE *output;
    
    switch (level) {
        case LOG_INFO:
            level_str = "INFO";
            output = stdout;
            break;
        case LOG_WARN:
            level_str = "WARN";
            output = stderr;
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            output = stderr;
            break;
        default:
            level_str = "UNKNOWN";
            output = stderr;
            break;
    }
    
    fprintf(output, "[%s] ", level_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
}

int validate_date_format(const char *date) {
    if (!date || strlen(date) != 10) {
        return 0;
    }
    
    // Check YYYY-MM-DD format
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (date[i] != '-') return 0;
        } else {
            if (date[i] < '0' || date[i] > '9') return 0;
        }
    }
    
    // Additional validation - check if it's a valid date
    int year, month, day;
    if (sscanf(date, "%d-%d-%d", &year, &month, &day) != 3) {
        return 0;
    }
    
    // Basic range checks
    if (year < 1900 || year > 2100) return 0;
    if (month < 1 || month > 12) return 0;
    if (day < 1 || day > 31) return 0;
    
    // More detailed month/day validation
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // Check for leap year
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        days_in_month[1] = 29;
    }
    
    if (day > days_in_month[month - 1]) return 0;
    
    return 1;
}

int compare_dates(const char *date1, const char *date2) {
    if (!date1 || !date2) {
        return 0; // Invalid comparison
    }
    
    // Simple string comparison works for ISO dates (YYYY-MM-DD)
    return strcmp(date1, date2);
}

char* safe_strdup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char *copy = malloc(len);
    if (!copy) {
        log_message(LOG_ERROR, "Memory allocation failed");
        return NULL;
    }
    
    memcpy(copy, str, len);
    return copy;
}

void safe_free(void **ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}
