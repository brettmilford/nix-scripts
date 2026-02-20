#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include "utils.h"
#include "config.h"
#include "transaction.h"
#include "categoriser.h"
#include "parsers/parser.h"
#include "parsers/cba_parser.h"
#include "xlsx_writer.h"
#include "paperless_api.h"

#define VERSION "1.0.0"

// Check if correspondent is CBA (for AI parsing)
static int is_cba_correspondent(const char *correspondent) {
    if (!correspondent) {
        log_message(LOG_INFO, "is_cba_correspondent: correspondent is NULL");
        return 0;
    }
    
    int is_cba = (strcmp(correspondent, "133") == 0 || 
                  strcmp(correspondent, "CBA") == 0 || 
                  strcmp(correspondent, "Commonwealth Bank") == 0);
    
    log_message(LOG_INFO, "is_cba_correspondent: correspondent='%s', is_cba=%s", 
                correspondent, is_cba ? "YES" : "NO");
    
    return is_cba;
}

static void print_usage(const char *program_name) {
    printf("Statement Processor v%s\n", VERSION);
    printf("Usage: %s --date-from <YYYY-MM-DD> --date-to <YYYY-MM-DD> [-c <config>] [-o <output_dir>] [--reprocess]\n\n", program_name);
    printf("Arguments:\n");
    printf("  --date-from <date>    Start date (ISO format: YYYY-MM-DD, inclusive)\n");
    printf("  --date-to <date>      End date (ISO format: YYYY-MM-DD, inclusive)\n");
    printf("  -c <path>             Path to configuration file\n");
    printf("  -o <path>             Output directory (default: current directory)\n");
    printf("  --reprocess           Include documents already tagged as \"processed\"\n");
    printf("  -h, --help            Show this help message\n");
    printf("  --version             Show version information\n\n");
    printf("Environment Variables:\n");
    printf("  PAPERLESS_URL         Base URL of Paperless instance (required)\n");
    printf("  PAPERLESS_API_KEY     API authentication token (required)\n\n");
    printf("Examples:\n");
    printf("  %s --date-from 2024-01-01 --date-to 2024-01-31 -c sm-proc.cfg\n", program_name);
    printf("  %s --date-from 2024-01-01 --date-to 2024-01-31 -o ~/reports/\n", program_name);
    printf("  %s --date-from 2024-01-01 --date-to 2024-01-31 --reprocess\n", program_name);
}

int main(int argc, char *argv[]) {
    char *date_from = NULL;
    char *date_to = NULL;
    char *config_file = NULL;
    char *output_dir = ".";
    int reprocess = 0;
    
    static struct option long_options[] = {
        {"date-from", required_argument, 0, 1001},
        {"date-to", required_argument, 0, 1002},
        {"reprocess", no_argument, 0, 1003},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 1004},
        {0, 0, 0, 0}
    };
    
    int c;
    int option_index = 0;
    
    while ((c = getopt_long(argc, argv, "c:o:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 1001: // --date-from
                date_from = optarg;
                break;
            case 1002: // --date-to
                date_to = optarg;
                break;
            case 1003: // --reprocess
                reprocess = 1;
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'o':
                output_dir = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 1004: // --version
                printf("Statement Processor v%s\n", VERSION);
                return 0;
            case '?':
                fprintf(stderr, "Use --help for usage information\n");
                return 1;
            default:
                break;
        }
    }
    
    // Validate required arguments
    if (!date_from) {
        log_message(LOG_ERROR, "--date-from is required");
        return 1;
    }
    
    if (!date_to) {
        log_message(LOG_ERROR, "--date-to is required");
        return 1;
    }
    
    // Validate date formats
    if (!validate_date_format(date_from)) {
        log_message(LOG_ERROR, "Invalid date format for --date-from. Use YYYY-MM-DD");
        return 1;
    }
    
    if (!validate_date_format(date_to)) {
        log_message(LOG_ERROR, "Invalid date format for --date-to. Use YYYY-MM-DD");
        return 1;
    }
    
    // Check date ordering
    if (compare_dates(date_from, date_to) > 0) {
        log_message(LOG_ERROR, "--date-from must be <= --date-to");
        return 1;
    }
    
    // Check environment variables
    const char *paperless_url = getenv("PAPERLESS_URL");
    const char *paperless_api_key = getenv("PAPERLESS_API_KEY");
    
    if (!paperless_url) {
        log_message(LOG_ERROR, "PAPERLESS_URL environment variable is required");
        return 1;
    }
    
    if (!paperless_api_key) {
        log_message(LOG_ERROR, "PAPERLESS_API_KEY environment variable is required");
        return 1;
    }
    
    // Check output directory exists and is writable
    if (access(output_dir, W_OK) != 0) {
        log_message(LOG_ERROR, "Output directory '%s' is not writable", output_dir);
        return 1;
    }
    
    // Print startup information
    log_message(LOG_INFO, "Statement Processor v%s", VERSION);
    log_message(LOG_INFO, "Date range: %s to %s", date_from, date_to);
    log_message(LOG_INFO, "Paperless URL: %s", paperless_url);
    if (config_file) {
        log_message(LOG_INFO, "Configuration: %s", config_file);
    } else {
        log_message(LOG_INFO, "No configuration file specified (all transactions will be uncategorised)");
    }
    log_message(LOG_INFO, "Output directory: %s", output_dir);
    if (reprocess) {
        log_message(LOG_INFO, "Reprocess mode: including already processed documents");
    }
    
    // Load configuration
    Config *config = load_config(config_file);
    if (!config) {
        log_message(LOG_ERROR, "Failed to load configuration");
        return 1;
    }
    
    log_message(LOG_INFO, "Default category: %s", config->default_category);
    log_message(LOG_INFO, "Loaded %zu categorisation rules", config->rule_count);
    
    // Initialize CBA parser with configuration for AI processing
    set_cba_parser_config(config);
    
    // Main processing workflow: Paperless API → Parse → Categorize → XLSX
    log_message(LOG_INFO, "Starting main document processing workflow...");
    
    // Query documents from Paperless API
    int document_count = 0;
    PaperlessDocument *documents = query_documents(date_from, date_to, reprocess, &document_count);
    
    if (!documents || document_count == 0) {
        log_message(LOG_INFO, "No documents found for processing");
        free_config(config);
        return 0;
    }
    
    // Process each document
    Transaction *all_transactions = NULL;
    TransactionMetadata *all_metadata = NULL;
    size_t total_transaction_count = 0;
    int processed_documents = 0;
    int skipped_documents = 0;
    
    for (int i = 0; i < document_count; i++) {
        PaperlessDocument *doc = &documents[i];
        
        log_message(LOG_INFO, "Processing document %d/%d (ID: %d)", 
                   i + 1, document_count, doc->id);
        
        // Check if we have a correspondent
        if (!doc->correspondent) {
            log_message(LOG_WARN, "Document %d: Missing correspondent field, skipping", doc->id);
            skipped_documents++;
            continue;
        }
        
        // Find appropriate parser
        ParserFunc parser = get_parser_for_correspondent(doc->correspondent);
        if (!parser) {
            log_message(LOG_WARN, "Document %d: Unknown institution '%s', skipping", 
                       doc->id, doc->correspondent);
            skipped_documents++;
            continue;
        }
        
        // Parse the document - use extended CBA parser for CBA documents
        log_message(LOG_INFO, "About to check if correspondent '%s' is CBA for document %d", 
                   doc->correspondent ? doc->correspondent : "NULL", doc->id);
        
        ParseResult *parse_result;
        if (is_cba_correspondent(doc->correspondent)) {
            log_message(LOG_INFO, "Document %d: Using extended CBA parser with AI support", doc->id);
            // Use extended CBA parser that supports AI processing
            parse_result = parse_cba_statement_with_id(doc->content, doc->correspondent, doc->id);
        } else {
            log_message(LOG_INFO, "Document %d: Using standard parser for correspondent '%s'", 
                       doc->id, doc->correspondent ? doc->correspondent : "NULL");
            // Use standard parser for other institutions
            parse_result = parser(doc->content, doc->correspondent);
        }
        if (!parse_result || parse_result->transaction_count == 0) {
            log_message(LOG_WARN, "Document %d (%s): Failed to extract transactions", 
                       doc->id, doc->correspondent);
            if (parse_result) {
                free_parse_result(parse_result);
                free(parse_result);
            }
            skipped_documents++;
            continue;
        }
        
        log_message(LOG_INFO, "Document %d (%s): Extracted %zu transactions", 
                   doc->id, doc->correspondent, parse_result->transaction_count);
        
        // Categorize transactions
        categorise_all_transactions(parse_result->transactions, parse_result->transaction_count, config);
        
        // Expand the global transactions and metadata arrays
        all_transactions = realloc(all_transactions, 
                                  (total_transaction_count + parse_result->transaction_count) * sizeof(Transaction));
        all_metadata = realloc(all_metadata,
                              (total_transaction_count + parse_result->transaction_count) * sizeof(TransactionMetadata));
        
        if (!all_transactions || !all_metadata) {
            log_message(LOG_ERROR, "Memory allocation failed for transactions");
            free_parse_result(parse_result);
            free(parse_result);
            break;
        }
        
        // Deep copy transactions and populate metadata
        for (size_t j = 0; j < parse_result->transaction_count; j++) {
            Transaction *src = &parse_result->transactions[j];
            Transaction *dst = &all_transactions[total_transaction_count + j];
            TransactionMetadata *meta = &all_metadata[total_transaction_count + j];
            
            // Copy transaction data
            dst->date = src->date ? safe_strdup(src->date) : NULL;
            dst->description = src->description ? safe_strdup(src->description) : NULL;
            dst->category = src->category ? safe_strdup(src->category) : NULL;
            dst->debit = src->debit;
            dst->credit = src->credit;
            
            // Populate metadata
            if (strcmp(doc->correspondent, "133") == 0) {
                meta->institution = safe_strdup("Commonwealth Bank");
            } else if (strcmp(doc->correspondent, "11") == 0) {
                meta->institution = safe_strdup("ANZ");
            } else {
                meta->institution = safe_strdup("Unknown");
            }
            
            meta->account_number = parse_result->account_number ? safe_strdup(parse_result->account_number) : NULL;
            meta->document_id = doc->id;
        }
        total_transaction_count += parse_result->transaction_count;
        
        processed_documents++;
        
        // Clean up parse result
        free_parse_result(parse_result);
        free(parse_result);
    }
    
    log_message(LOG_INFO, "Document processing complete:");
    log_message(LOG_INFO, "  Documents processed: %d", processed_documents);
    log_message(LOG_INFO, "  Documents skipped: %d", skipped_documents);
    log_message(LOG_INFO, "  Total transactions: %zu", total_transaction_count);
    
    if (total_transaction_count > 0) {
        // Sort all transactions by date
        log_message(LOG_INFO, "Sorting %zu transactions by date...", total_transaction_count);
        // Note: We need a sorting function that works on Transaction array directly
        // For now, we'll skip sorting and generate the XLSX
        
        // Generate XLSX report with real data
        log_message(LOG_INFO, "Generating XLSX report with real transaction data...");
        char *xlsx_filename = generate_xlsx_filename(date_from, date_to, output_dir);
        if (xlsx_filename) {
            if (prompt_file_overwrite(xlsx_filename)) {
                if (create_xlsx_report(xlsx_filename, all_transactions, all_metadata, total_transaction_count, 
                                     date_from, date_to, paperless_url)) {
                    log_message(LOG_INFO, "XLSX file created successfully: %s", xlsx_filename);
                    
                    // Calculate and display final statistics
                    XLSXStats stats = calculate_xlsx_stats(all_transactions, total_transaction_count);
                    log_message(LOG_INFO, "Final Statistics:");
                    log_message(LOG_INFO, "  Total Transactions: %zu", stats.total_transactions);
                    log_message(LOG_INFO, "  Total Debits: $%.2f", stats.total_debit);
                    log_message(LOG_INFO, "  Total Credits: $%.2f", stats.total_credit);
                    log_message(LOG_INFO, "  Net Amount: $%.2f", stats.net_amount);
                    log_message(LOG_INFO, "  Categorised: %zu (%.1f%%)", stats.categorised_count,
                               stats.total_transactions > 0 ? (double)stats.categorised_count / stats.total_transactions * 100.0 : 0.0);
                    
                    // Tag documents as processed
                    log_message(LOG_INFO, "Tagging processed documents in Paperless...");
                    int tagged_count = 0;
                    for (int i = 0; i < document_count; i++) {
                        if (update_document_tags(documents[i].id, date_from, date_to)) {
                            tagged_count++;
                        }
                    }
                    log_message(LOG_INFO, "Tagged %d/%d documents as processed", tagged_count, processed_documents);
                    
                } else {
                    log_message(LOG_ERROR, "Failed to create XLSX file");
                }
            } else {
                log_message(LOG_INFO, "XLSX file creation cancelled by user");
            }
            free(xlsx_filename);
        }
        
        // Free transaction data and metadata
        for (size_t i = 0; i < total_transaction_count; i++) {
            safe_free((void**)&all_transactions[i].date);
            safe_free((void**)&all_transactions[i].description);
            safe_free((void**)&all_transactions[i].category);
            
            // Free metadata
            safe_free((void**)&all_metadata[i].institution);
            safe_free((void**)&all_metadata[i].account_number);
        }
        free(all_transactions);
        free(all_metadata);
    }
    
    // Clean up documents
    for (int i = 0; i < document_count; i++) {
        free_paperless_document(&documents[i]);
    }
    free(documents);
    
    log_message(LOG_INFO, "Processing workflow complete");
    
    // Clean up
    free_config(config);
    
    return 0;
}
