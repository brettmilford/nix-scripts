#ifndef PAPERLESS_API_H
#define PAPERLESS_API_H

#include "transaction.h"
#include <stddef.h>

typedef struct {
    int id;
    char *correspondent;  // May be NULL
    char *content;        // Full text content
    char *created_date;   // ISO format YYYY-MM-DD
} PaperlessDocument;

typedef struct {
    int documents_queried;
    int documents_processed;
    int documents_skipped_unknown;
    int documents_skipped_parse_error;
    int documents_skipped_no_correspondent;
    int total_transactions;
    int documents_tagged_success;
    int documents_tagged_failed;
} ProcessingStats;

// API functions
PaperlessDocument* query_documents(const char *date_from, const char *date_to, int reprocess, int *count);
int update_document_tags(int document_id, const char *date_from, const char *date_to);
void free_paperless_document(PaperlessDocument *doc);

#endif // PAPERLESS_API_H
