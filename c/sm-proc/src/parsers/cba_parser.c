#include "cba_parser.h"
#include "../utils.h"
#include "../transaction.h"
#include "../config.h"
#include "../ai/ai_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <curl/curl.h>

// Global config pointer (set by main application)
static Config *g_config = NULL;

// Set global config for CBA parser
void set_cba_parser_config(Config *config) {
    g_config = config;
}

// Check if AI parsing is configured for CBA
static int should_use_ai_parsing(void) {
    if (!g_config || !g_config->cba_config) {
        log_message(LOG_INFO, "CBA parser: No configuration available, using content parsing");
        return 0; // Default to content parsing if no config
    }
    
    if (!g_config->cba_config->method) {
        log_message(LOG_INFO, "CBA parser: No method configured, using content parsing");
        return 0;
    }
    
    int use_ai = (strcmp(g_config->cba_config->method, "ai") == 0);
    log_message(LOG_INFO, "CBA parser: Method configured as '%s', AI parsing: %s", 
                g_config->cba_config->method, use_ai ? "ENABLED" : "DISABLED");
    
    return use_ai;
}

// Create AI service config from loaded configuration
static AIServiceConfig* create_cba_ai_config(void) {
    if (!g_config || !g_config->cba_config) {
        log_message(LOG_ERROR, "CBA parser: No configuration available");
        return NULL;
    }
    
    const char *provider = g_config->cba_config->provider;
    if (!provider) {
        log_message(LOG_ERROR, "CBA parser: No AI provider configured");
        return NULL;
    }
    
    AIProviderConfig *provider_config = NULL;
    if (strcmp(provider, "anthropic") == 0) {
        provider_config = g_config->anthropic_config;
    } else if (strcmp(provider, "openrouter") == 0) {
        provider_config = g_config->openrouter_config;
    } else if (strcmp(provider, "llamacpp") == 0) {
        provider_config = g_config->llamacpp_config;
    }
    
    if (!provider_config) {
        log_message(LOG_ERROR, "CBA parser: Provider '%s' not configured", provider);
        return NULL;
    }
    
    // Get API key from environment variable
    const char *api_key = NULL;
    if (provider_config->api_key_env) {
        api_key = getenv(provider_config->api_key_env);
    }
    
    return create_ai_service_config(provider, provider_config->model, api_key, provider_config->base_url);
}

// AI-powered PDF parsing for CBA statements
static ParseResult* parse_cba_with_ai(const char *pdf_path) {
    log_message(LOG_INFO, "CBA parser: Using AI service for PDF extraction");
    
    AIServiceConfig *config = create_cba_ai_config();
    if (!config) {
        log_message(LOG_ERROR, "CBA parser: Failed to create AI service configuration");
        return NULL;
    }
    
    if (!config->api_key) {
        log_message(LOG_ERROR, "CBA parser: No API key configured for AI service");
        free_ai_service_config(config);
        return NULL;
    }
    
    ParseResult *result = ai_service_parse_pdf(pdf_path, config);
    free_ai_service_config(config);
    
    if (!result) {
        log_message(LOG_ERROR, "CBA parser: AI service failed to parse PDF");
        return NULL;
    }
    
    log_message(LOG_INFO, "CBA parser: AI service extracted %zu transactions", result->transaction_count);
    return result;
}

// Write callback for libcurl to save PDF to file
static size_t write_pdf_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    FILE *fp = (FILE *)userp;
    return fwrite(contents, size, nmemb, fp);
}

// Download PDF file from Paperless API for AI processing
static char* download_pdf_from_paperless(int document_id) {
    const char *paperless_url = getenv("PAPERLESS_URL");
    const char *paperless_api_key = getenv("PAPERLESS_API_KEY");
    
    if (!paperless_url || !paperless_api_key) {
        log_message(LOG_ERROR, "CBA parser: Missing Paperless API configuration");
        return NULL;
    }
    
    // Create download URL for the original PDF
    char download_url[500];
    snprintf(download_url, sizeof(download_url), "%s/api/documents/%d/download/", paperless_url, document_id);
    
    // Create temporary filename
    char *temp_filename = malloc(64);
    if (!temp_filename) return NULL;
    snprintf(temp_filename, 64, "/tmp/cba_statement_%d.pdf", document_id);
    
    // Download the PDF file
    log_message(LOG_INFO, "CBA parser: Downloading PDF for document %d", document_id);
    
    CURL *curl;
    CURLcode res;
    FILE *fp;
    
    curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "CBA parser: Failed to initialize curl");
        free(temp_filename);
        return NULL;
    }
    
    fp = fopen(temp_filename, "wb");
    if (!fp) {
        log_message(LOG_ERROR, "CBA parser: Failed to create temporary file %s", temp_filename);
        curl_easy_cleanup(curl);
        free(temp_filename);
        return NULL;
    }
    
    // Set up curl options
    curl_easy_setopt(curl, CURLOPT_URL, download_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_pdf_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Set authorization header
    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Token %s", paperless_api_key);
    headers = curl_slist_append(headers, auth_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform the download
    res = curl_easy_perform(curl);
    
    fclose(fp);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        log_message(LOG_ERROR, "CBA parser: PDF download failed: %s", curl_easy_strerror(res));
        unlink(temp_filename);
        free(temp_filename);
        return NULL;
    }
    
    log_message(LOG_INFO, "CBA parser: PDF downloaded to %s", temp_filename);
    return temp_filename;
}

// Helper function to extract account number
static char* extract_account_number(const char *content) {
    const char *search_str = "Account Number";
    char *found = strstr(content, search_str);
    
    if (!found) return NULL;
    
    // Skip past "Account Number" and any whitespace
    char *start = found + strlen(search_str);
    while (*start && (isspace(*start) || *start == ':')) start++;
    
    // Find end of account number (until newline or significant whitespace)
    char *end = start;
    while (*end && *end != '\n' && *end != '\r') {
        if (isspace(*end)) {
            // Check if this is just internal spacing (like "06 4144 10181166")
            char *temp = end;
            while (*temp && isspace(*temp)) temp++;
            if (*temp && isdigit(*temp)) {
                end = temp; // Continue, this is internal spacing
                continue;
            } else {
                break; // End of account number
            }
        }
        end++;
    }
    
    // Extract and return account number
    size_t len = end - start;
    if (len == 0) return NULL;
    
    char *account = malloc(len + 1);
    if (!account) return NULL;
    
    strncpy(account, start, len);
    account[len] = '\0';
    
    // Trim trailing whitespace
    char *trim_end = account + len - 1;
    while (trim_end > account && isspace(*trim_end)) {
        *trim_end = '\0';
        trim_end--;
    }
    
    return account;
}

// Helper function to extract statement period for year resolution
static char* extract_statement_period(const char *content) {
    const char *search_str = "Statement Period";
    char *found = strstr(content, search_str);
    
    if (!found) return NULL;
    
    // Skip past "Statement Period" and any whitespace/colon
    char *start = found + strlen(search_str);
    while (*start && (isspace(*start) || *start == ':')) start++;
    
    // Find end of line
    char *end = start;
    while (*end && *end != '\n' && *end != '\r') end++;
    
    size_t len = end - start;
    if (len == 0) return NULL;
    
    char *period = malloc(len + 1);
    if (!period) return NULL;
    
    strncpy(period, start, len);
    period[len] = '\0';
    
    // Trim trailing whitespace
    char *trim_end = period + len - 1;
    while (trim_end > period && isspace(*trim_end)) {
        *trim_end = '\0';
        trim_end--;
    }
    
    return period;
}

// Helper function to parse CBA date (handles DD MMM format with year from statement period)
static char* parse_cba_date(const char *date_str, const char *statement_period) {
    if (!date_str) return NULL;
    
    // Parse "DD MMM" format (year comes from statement period)
    int day;
    char month_str[10];
    if (sscanf(date_str, "%d %9s", &day, month_str) != 2) return NULL;
    
    // Convert month name to number
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = -1;
    for (int i = 0; i < 12; i++) {
        if (strcasecmp(month_str, months[i]) == 0) {
            month = i + 1;
            break;
        }
    }
    if (month == -1) return NULL;
    
    // Extract year from statement period
    int start_year = 2025, end_year = 2025; // Default fallback
    if (statement_period) {
        // Parse "1 May 2025 - 31 Oct 2025" format
        sscanf(statement_period, "%*d %*s %d - %*d %*s %d", &start_year, &end_year);
    }
    
    // Determine correct year for this transaction
    int year = start_year;
    if (month <= 6 && start_year != end_year) {
        // If month is in first half of year and statement spans years, might be end year
        year = end_year;
    }
    
    // Validate date components
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 1900) return NULL;
    
    // Format as ISO date
    char *iso_date = malloc(11); // YYYY-MM-DD + null
    if (!iso_date) return NULL;
    
    snprintf(iso_date, 11, "%04d-%02d-%02d", year, month, day);
    return iso_date;
}

// Forward declaration for parse_cba_amount
static int parse_cba_amount(const char *amount_str, double *debit, double *credit);

// Helper function to process a complete transaction line in CBA table format
static void process_cba_transaction_line(const char *transaction_line, ParseResult *result) {
    if (!transaction_line || !result) return;
    
    // CBA format from PDF: "Date Transaction Debit Credit Balance"
    // Example: "17 May Transfer To Mr Brett Christopher Milford CommBank App Mortgage 6,677.00 $10,819.79 CR"
    
    char *line_copy = safe_strdup(transaction_line);
    if (!line_copy) return;
    
    // Parse date (first two tokens: DD MMM)
    int day;
    char month_str[10];
    if (sscanf(line_copy, "%d %9s", &day, month_str) != 2) {
        free(line_copy);
        return;
    }
    
    // Format date string and parse to ISO using statement period for year
    char date_str[20];
    snprintf(date_str, sizeof(date_str), "%d %s", day, month_str);
    char *iso_date = parse_cba_date(date_str, result->statement_period);
    if (!iso_date) {
        free(line_copy);
        return;
    }
    
    // Find where description starts (after date: DD MMM)
    const char *desc_start = transaction_line;
    int field_count = 0;
    while (*desc_start && field_count < 2) {
        // Skip current field
        while (*desc_start && !isspace(*desc_start)) desc_start++;
        // Skip whitespace to next field
        while (*desc_start && isspace(*desc_start)) desc_start++;
        field_count++;
    }
    
    // Parse amounts based on CBA table format
    double debit = 0.0, credit = 0.0;
    char description[500] = "";
    
    // Find the balance at the end (pattern: "$amount CR")
    const char *balance_pos = strstr(transaction_line, " CR");
    if (!balance_pos) {
        free(line_copy);
        safe_free((void**)&iso_date);
        return;
    }
    
    // Find the $ before " CR" to locate balance
    const char *balance_dollar = balance_pos;
    while (balance_dollar > transaction_line && *balance_dollar != '$') {
        balance_dollar--;
    }
    
    // Now work backwards to find transaction amounts before the balance
    const char *amounts_end = balance_dollar;
    while (amounts_end > desc_start && isspace(*(amounts_end - 1))) {
        amounts_end--;
    }
    
    // Look for debit amount (embedded in description with " (" pattern or at end without $)
    const char *debit_pattern = strstr(transaction_line, " (");
    if (debit_pattern && debit_pattern < amounts_end) {
        // Found " (" pattern - look backwards for the amount
        const char *debit_end = debit_pattern;
        const char *debit_start = debit_end;
        
        // Scan backwards to find debit amount before " ("
        while (debit_start > desc_start && 
               (isdigit(*(debit_start-1)) || *(debit_start-1) == ',' || *(debit_start-1) == '.')) {
            debit_start--;
        }
        
        if (debit_start < debit_end && isdigit(*debit_start)) {
            // Extract debit amount
            char debit_str[50];
            size_t debit_len = debit_end - debit_start;
            if (debit_len > 0 && debit_len < 49) {
                strncpy(debit_str, debit_start, debit_len);
                debit_str[debit_len] = '\0';
                
                // Parse debit amount
                double temp_debit, temp_credit;
                if (parse_cba_amount(debit_str, &temp_debit, &temp_credit)) {
                    debit = temp_credit; // Amount is in debit column
                }
            }
            
            // Description ends before the debit amount
            amounts_end = debit_start;
            while (amounts_end > desc_start && isspace(*(amounts_end - 1))) {
                amounts_end--;
            }
        }
    } else {
        // Look for debit amount at the end (no $ prefix, just number)
        const char *debit_end = amounts_end;
        const char *debit_start = debit_end;
        
        // Scan backwards to find debit amount
        while (debit_start > desc_start && 
               (isdigit(*(debit_start-1)) || *(debit_start-1) == ',' || *(debit_start-1) == '.')) {
            debit_start--;
        }
        
        if (debit_start < debit_end && isdigit(*debit_start)) {
            // Extract debit amount
            char debit_str[50];
            size_t debit_len = debit_end - debit_start;
            if (debit_len > 0 && debit_len < 49) {
                strncpy(debit_str, debit_start, debit_len);
                debit_str[debit_len] = '\0';
                
                // Parse debit amount
                double temp_debit, temp_credit;
                if (parse_cba_amount(debit_str, &temp_debit, &temp_credit)) {
                    debit = temp_credit; // Amount is in debit column
                }
            }
            
            // Description ends before the debit amount
            amounts_end = debit_start;
            while (amounts_end > desc_start && isspace(*(amounts_end - 1))) {
                amounts_end--;
            }
        }
    }
    
    // Look for credit amount ($ prefix)
    const char *credit_pos = NULL;
    const char *search_pos = desc_start;
    
    // Find the last $ before the balance (this would be the credit amount)
    while (search_pos < amounts_end) {
        const char *dollar = strchr(search_pos, '$');
        if (!dollar || dollar >= amounts_end) break;
        
        // Check if this $ is followed by digits (not the balance)
        const char *after_dollar = dollar + 1;
        if (isdigit(*after_dollar)) {
            credit_pos = dollar;
        }
        search_pos = dollar + 1;
    }
    
    if (credit_pos && credit_pos < amounts_end) {
        // Extract credit amount
        const char *credit_start = credit_pos;
        const char *credit_end = credit_start + 1;
        
        while (*credit_end && credit_end < amounts_end && 
               (isdigit(*credit_end) || *credit_end == ',' || *credit_end == '.')) {
            credit_end++;
        }
        
        char credit_str[50];
        size_t credit_len = credit_end - credit_start;
        if (credit_len > 0 && credit_len < 49) {
            strncpy(credit_str, credit_start, credit_len);
            credit_str[credit_len] = '\0';
            
            // Parse credit amount  
            double temp_debit, temp_credit;
            if (parse_cba_amount(credit_str, &temp_debit, &temp_credit)) {
                credit = temp_credit; // Amount is in credit column
            }
        }
        
        // Description ends before the credit amount
        amounts_end = credit_start;
        while (amounts_end > desc_start && isspace(*(amounts_end - 1))) {
            amounts_end--;
        }
    }
    
    // Extract description (from after date to before amounts)
    size_t desc_len = amounts_end - desc_start;
    if (desc_len > 0 && desc_len < 499) {
        strncpy(description, desc_start, desc_len);
        description[desc_len] = '\0';
        
        // Trim trailing whitespace
        char *trim_end = description + strlen(description) - 1;
        while (trim_end > description && isspace(*trim_end)) {
            *trim_end = '\0';
            trim_end--;
        }
    }
    
    // Add transaction if valid
    if (strlen(description) > 0 && (debit > 0.0 || credit > 0.0)) {
        log_message(LOG_INFO, "CBA: Parsed transaction - Date: %s, Desc: '%s', Debit: %.2f, Credit: %.2f", 
                   iso_date, description, debit, credit);
        add_transaction_to_result(result, iso_date, description, debit, credit, NULL);
    } else {
        log_message(LOG_WARN, "CBA: Skipping invalid transaction - Desc: '%s', Debit: %.2f, Credit: %.2f", 
                   description, debit, credit);
    }
    
    safe_free((void**)&iso_date);
    free(line_copy);
}

// Helper function to parse amount (handles CBA format)
static int parse_cba_amount(const char *amount_str, double *debit, double *credit) {
    if (!amount_str || !debit || !credit) return 0;
    
    *debit = 0.0;
    *credit = 0.0;
    
    // Clean the amount string - remove commas and dollar signs
    char cleaned[100];
    int cleaned_pos = 0;
    
    for (int i = 0; amount_str[i] && cleaned_pos < 99; i++) {
        char c = amount_str[i];
        if (c == ',' || c == '$') {
            continue; // Remove commas and dollar signs
        }
        if (c == ' ' && cleaned_pos > 0 && cleaned[cleaned_pos-1] == ' ') {
            continue; // Skip multiple spaces
        }
        cleaned[cleaned_pos++] = c;
    }
    cleaned[cleaned_pos] = '\0';
    
    // Trim whitespace
    char *start = cleaned;
    while (*start && isspace(*start)) start++;
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        *end = '\0';
        end--;
    }
    
    if (strlen(start) == 0) return 0;
    
    // Parse the numeric value
    double amount;
    if (sscanf(start, "%lf", &amount) != 1) return 0;
    
    // In CBA format based on PDF:
    // - Amounts in "Debit" column are debits (no $ prefix)
    // - Amounts in "Credit" column are credits ($ prefix)
    // Since we're extracting by position, we just return the amount
    // The calling function determines debit vs credit based on column position
    
    // For now, always return as credit - the caller will handle classification
    *credit = amount;
    
    return 1;
}

ParseResult* parse_cba_statement(const char *content, const char *correspondent __attribute__((unused))) {
    if (!content) {
        log_message(LOG_ERROR, "CBA parser: No content provided");
        return NULL;
    }
    
    // Check if AI parsing is configured for CBA
    if (should_use_ai_parsing()) {
        // For AI parsing, we need the PDF file, not text content
        // This is a limitation of the current parser interface
        // TODO: Modify parser interface to pass document ID for PDF download
        log_message(LOG_WARN, "CBA parser: AI parsing configured but PDF access not yet implemented");
        log_message(LOG_INFO, "CBA parser: Falling back to text-based parsing");
    }
    
    log_message(LOG_INFO, "Parsing CBA statement...");
    
    ParseResult *result = create_parse_result();
    if (!result) return NULL;
    
    // Extract account number
    result->account_number = extract_account_number(content);
    if (result->account_number) {
        log_message(LOG_INFO, "CBA account number: %s", result->account_number);
    }
    
    // Extract statement period for year resolution
    result->statement_period = extract_statement_period(content);
    if (result->statement_period) {
        log_message(LOG_INFO, "CBA statement period: %s", result->statement_period);
    }
    
    // Parse transactions
    char *content_copy = safe_strdup(content);
    if (!content_copy) {
        set_parse_result_error(result, "Failed to copy content for parsing");
        return result;
    }
    
    char *line = strtok(content_copy, "\n\r");
    char *current_transaction = NULL;
    
    while (line) {
        // Trim leading whitespace
        while (*line && isspace(*line)) line++;
        
        // Skip empty lines
        if (*line == '\0') {
            line = strtok(NULL, "\n\r");
            continue;
        }
        
        // Check if line starts with date pattern "DD MMM"
        int day;
        char month[10];
        if (sscanf(line, "%d %9s", &day, month) == 2 && day >= 1 && day <= 31) {
            // This looks like a transaction start
            
            // Process previous transaction if any
            if (current_transaction) {
                process_cba_transaction_line(current_transaction, result);
                free(current_transaction);
                current_transaction = NULL;
            }
            
            // Start new transaction
            current_transaction = safe_strdup(line);
        } else if (current_transaction) {
            // This line is a continuation of the current transaction
            size_t old_len = strlen(current_transaction);
            size_t new_len = old_len + strlen(line) + 2; // +2 for space and null
            
            char *expanded = realloc(current_transaction, new_len);
            if (expanded) {
                current_transaction = expanded;
                strcat(current_transaction, " ");
                strcat(current_transaction, line);
            }
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    // Process final transaction if any
    if (current_transaction) {
        process_cba_transaction_line(current_transaction, result);
        free(current_transaction);
    }
    
    free(content_copy);
    
    log_message(LOG_INFO, "CBA parser: Extracted %zu transactions", result->transaction_count);
    return result;
}

// Extended CBA parser with document ID for AI-powered PDF processing
ParseResult* parse_cba_statement_with_id(const char *content, const char *correspondent, int document_id) {
    log_message(LOG_INFO, "CBA parser: Extended parser called for document ID %d", document_id);
    
    if (!content) {
        log_message(LOG_ERROR, "CBA parser: No content provided");
        return NULL;
    }
    
    // Check if AI parsing is configured for CBA
    if (should_use_ai_parsing()) {
        log_message(LOG_INFO, "CBA parser: AI parsing enabled, processing PDF for document %d", document_id);
        
        // Download PDF from Paperless
        char *pdf_path = download_pdf_from_paperless(document_id);
        if (!pdf_path) {
            log_message(LOG_ERROR, "CBA parser: Failed to download PDF, falling back to text parsing");
            return parse_cba_statement(content, correspondent);
        }
        
        log_message(LOG_INFO, "CBA parser: PDF downloaded to %s, sending to AI service", pdf_path);
        
        // Use AI service to parse the PDF
        ParseResult *ai_result = parse_cba_with_ai(pdf_path);
        
        // Clean up temporary PDF file
        if (pdf_path) {
            unlink(pdf_path); // Delete temporary file
            free(pdf_path);
        }
        
        if (ai_result) {
            return ai_result;
        } else {
            log_message(LOG_ERROR, "CBA parser: AI parsing failed, falling back to text parsing");
            return parse_cba_statement(content, correspondent);
        }
    } else {
        // Use standard text-based parsing
        return parse_cba_statement(content, correspondent);
    }
}
