#ifndef CATEGORISER_H
#define CATEGORISER_H

#include "config.h"
#include "transaction.h"

// Core categorisation functions
void categorise_transaction(Transaction *transaction, const Config *config);
void categorise_all_transactions(Transaction *transactions, size_t count, const Config *config);

// Utility functions
const char* get_transaction_category(const Transaction *transaction);
int is_transaction_categorised(const Transaction *transaction, const Config *config);

// Statistics and reporting
void print_categorisation_stats(const Transaction *transactions, size_t count, const Config *config);

#endif // CATEGORISER_H
