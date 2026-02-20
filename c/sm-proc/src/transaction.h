#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stddef.h>

typedef struct {
    char *date;           // ISO format YYYY-MM-DD, dynamically allocated
    char *description;    // Full description, dynamically allocated
    double debit;         // 0.0 if credit transaction
    double credit;        // 0.0 if debit transaction
    char *category;       // Categorisation result, dynamically allocated
} Transaction;

typedef struct {
    char *account_number;          // Extracted account number
    char *statement_period;        // For date resolution (optional)
    Transaction *transactions;     // Array of transactions
    size_t transaction_count;      // Number of transactions
    char *error_message;           // NULL if success, error string if failed
} ParseResult;

// Transaction creation and management functions
Transaction* create_transaction(const char *date, const char *description, 
                               double debit, double credit, const char *category);
void free_transaction(Transaction *t);

// ParseResult creation and management functions  
ParseResult* create_parse_result(void);
void free_parse_result(ParseResult *pr);
int add_transaction_to_result(ParseResult *pr, const char *date, const char *description,
                             double debit, double credit, const char *category);
void set_parse_result_error(ParseResult *pr, const char *error_message);

// Transaction sorting functions
int compare_transactions_by_date(const void *a, const void *b);
void sort_transactions(Transaction *transactions, size_t count);
void sort_parse_result_transactions(ParseResult *pr);

#endif // TRANSACTION_H
