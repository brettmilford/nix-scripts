#include "transaction.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

void free_transaction(Transaction *t) {
    if (!t) return;
    
    safe_free((void**)&t->date);
    safe_free((void**)&t->description);
    safe_free((void**)&t->category);
    
    // Reset numeric fields
    t->debit = 0.0;
    t->credit = 0.0;
}

Transaction* create_transaction(const char *date, const char *description, 
                               double debit, double credit, const char *category) {
    Transaction *t = malloc(sizeof(Transaction));
    if (!t) {
        log_message(LOG_ERROR, "Failed to allocate memory for transaction");
        return NULL;
    }
    
    // Initialize to zero/NULL
    memset(t, 0, sizeof(Transaction));
    
    // Copy strings
    if (date) {
        t->date = safe_strdup(date);
        if (!t->date) {
            free(t);
            return NULL;
        }
    }
    
    if (description) {
        t->description = safe_strdup(description);
        if (!t->description) {
            free_transaction(t);
            free(t);
            return NULL;
        }
    }
    
    if (category) {
        t->category = safe_strdup(category);
        if (!t->category) {
            free_transaction(t);
            free(t);
            return NULL;
        }
    }
    
    // Set amounts
    t->debit = debit;
    t->credit = credit;
    
    return t;
}

void free_parse_result(ParseResult *pr) {
    if (!pr) return;
    
    safe_free((void**)&pr->account_number);
    safe_free((void**)&pr->statement_period);
    safe_free((void**)&pr->error_message);
    
    // Free all transactions
    if (pr->transactions) {
        for (size_t i = 0; i < pr->transaction_count; i++) {
            free_transaction(&pr->transactions[i]);
        }
        free(pr->transactions);
        pr->transactions = NULL;
    }
    
    pr->transaction_count = 0;
}

ParseResult* create_parse_result(void) {
    ParseResult *pr = malloc(sizeof(ParseResult));
    if (!pr) {
        log_message(LOG_ERROR, "Failed to allocate memory for parse result");
        return NULL;
    }
    
    // Initialize to zero/NULL
    memset(pr, 0, sizeof(ParseResult));
    
    return pr;
}

int add_transaction_to_result(ParseResult *pr, const char *date, const char *description,
                             double debit, double credit, const char *category) {
    if (!pr) return 0;
    
    // Reallocate transactions array
    Transaction *new_transactions = realloc(pr->transactions, 
                                          (pr->transaction_count + 1) * sizeof(Transaction));
    if (!new_transactions) {
        log_message(LOG_ERROR, "Failed to allocate memory for new transaction");
        return 0;
    }
    
    pr->transactions = new_transactions;
    
    // Initialize new transaction
    Transaction *new_t = &pr->transactions[pr->transaction_count];
    memset(new_t, 0, sizeof(Transaction));
    
    // Copy data
    if (date) {
        new_t->date = safe_strdup(date);
        if (!new_t->date) return 0;
    }
    
    if (description) {
        new_t->description = safe_strdup(description);
        if (!new_t->description) {
            safe_free((void**)&new_t->date);
            return 0;
        }
    }
    
    if (category) {
        new_t->category = safe_strdup(category);
        if (!new_t->category) {
            safe_free((void**)&new_t->date);
            safe_free((void**)&new_t->description);
            return 0;
        }
    }
    
    new_t->debit = debit;
    new_t->credit = credit;
    
    pr->transaction_count++;
    return 1;
}

int compare_transactions_by_date(const void *a, const void *b) {
    const Transaction *t1 = (const Transaction *)a;
    const Transaction *t2 = (const Transaction *)b;
    
    // Handle NULL dates
    if (!t1->date && !t2->date) return 0;
    if (!t1->date) return -1;  // NULL dates come first
    if (!t2->date) return 1;
    
    // Compare ISO dates (YYYY-MM-DD format allows string comparison)
    int date_cmp = strcmp(t1->date, t2->date);
    if (date_cmp != 0) return date_cmp;
    
    // If dates are equal, sort by description for consistent ordering
    if (t1->description && t2->description) {
        return strcmp(t1->description, t2->description);
    }
    
    // Handle NULL descriptions
    if (!t1->description && !t2->description) return 0;
    if (!t1->description) return -1;
    if (!t2->description) return 1;
    
    return 0;
}

void sort_transactions(Transaction *transactions, size_t count) {
    if (!transactions || count == 0) return;
    
    qsort(transactions, count, sizeof(Transaction), compare_transactions_by_date);
}

void sort_parse_result_transactions(ParseResult *pr) {
    if (!pr || !pr->transactions) return;
    
    sort_transactions(pr->transactions, pr->transaction_count);
}

void set_parse_result_error(ParseResult *pr, const char *error_message) {
    if (!pr) return;
    
    safe_free((void**)&pr->error_message);
    
    if (error_message) {
        pr->error_message = safe_strdup(error_message);
    }
}
