#ifndef UTILS_H
#define UTILS_H

// Logging levels
typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

// Logging functions
void log_message(LogLevel level, const char *format, ...);

// Date validation
int validate_date_format(const char *date);
int compare_dates(const char *date1, const char *date2);

// String utilities
char* safe_strdup(const char *str);
void safe_free(void **ptr);

#endif // UTILS_H
