#ifndef XLSX_WRITER_H
#define XLSX_WRITER_H

#include "transaction.h"
#include <stddef.h>
#include <stdbool.h>

// Transaction metadata for XLSX
typedef struct {
    char *institution;
    char *account_number;
    int document_id;
} TransactionMetadata;

// XLSX file generation
int create_xlsx_report(
    const char *filename,
    Transaction *transactions,
    TransactionMetadata *metadata,
    size_t transaction_count,
    const char *date_from,
    const char *date_to,
    const char *paperless_url
);

// Check if file exists and prompt for overwrite
bool prompt_file_overwrite(const char *filename);

// Generate filename from date range
char* generate_xlsx_filename(const char *date_from, const char *date_to, const char *output_dir);

// Statistics for XLSX generation
typedef struct {
    size_t total_transactions;
    double total_debit;
    double total_credit;
    double net_amount;
    size_t categorised_count;
    size_t uncategorised_count;
} XLSXStats;

// Calculate statistics from transactions
XLSXStats calculate_xlsx_stats(Transaction *transactions, size_t count);

#endif // XLSX_WRITER_H
