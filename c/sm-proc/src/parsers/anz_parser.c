#include "anz_parser.h"
#include "../utils.h"
#include "../transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper function to extract account number from ANZ statement
static char* extract_anz_account_number(const char *content) {
    const char *search_str = "ACCOUNT NUMBER:";
    char *found = strstr(content, search_str);
    
    if (!found) return NULL;
    
    // Skip past "ACCOUNT NUMBER:" and any whitespace
    char *start = found + strlen(search_str);
    while (*start && isspace(*start)) start++;
    
    // Find end of account number (until newline or significant whitespace)
    char *end = start;
    while (*end && *end != '\n' && *end != '\r') {
        if (isspace(*end)) {
            // Check if this is just internal spacing in the account number
            char *temp = end;
            while (*temp && isspace(*temp)) temp++;
            if (*temp && (isdigit(*temp) || *temp == '-')) {
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

// Helper function to parse ANZ date DD/MM/YYYY to ISO format
static char* parse_anz_date(const char *date_str) {
    if (!date_str) return NULL;
    
    int day, month, year;
    if (sscanf(date_str, "%d/%d/%d", &day, &month, &year) != 3) return NULL;
    
    // Validate date components
    if (day < 1 || day > 31 || month < 1 || month > 12 || year < 1900) return NULL;
    
    // Format as ISO date
    char *iso_date = malloc(11); // YYYY-MM-DD + null
    if (!iso_date) return NULL;
    
    snprintf(iso_date, 11, "%04d-%02d-%02d", year, month, day);
    return iso_date;
}

// Helper function to parse ANZ amount (handles CR suffix for credit)
static int parse_anz_amount(const char *amount_str, double *debit, double *credit) {
    if (!amount_str || !debit || !credit) return 0;
    
    *debit = 0.0;
    *credit = 0.0;
    
    // Make a copy to work with
    char cleaned[100];
    int cleaned_pos = 0;
    int has_cr = 0;
    
    // Remove currency symbols, commas, and check for CR suffix
    for (int i = 0; amount_str[i] && cleaned_pos < 99; i++) {
        char c = amount_str[i];
        if (c == ',' || c == '$') {
            continue;
        }
        if (c == 'C' && amount_str[i+1] == 'R') {
            has_cr = 1;
            break; // Stop parsing, found CR suffix
        }
        if (c == ' ' && cleaned_pos > 0 && cleaned[cleaned_pos-1] == ' ') {
            continue; // Skip multiple spaces
        }
        cleaned[cleaned_pos++] = c;
    }
    cleaned[cleaned_pos] = '\0';
    
    // Parse the numeric value
    double amount;
    if (sscanf(cleaned, "%lf", &amount) != 1) return 0;
    
    // Determine if debit or credit based on CR suffix
    if (has_cr) {
        *credit = amount;  // CR suffix indicates credit
    } else {
        *debit = amount;   // No suffix indicates debit
    }
    
    return 1;
}

// Helper function to check if a line looks like an ANZ transaction
static int is_anz_transaction_line(const char *line) {
    if (!line) return 0;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    
    // Check for date pattern DD/MM/YYYY at start
    int day, month, year;
    return (sscanf(line, "%d/%d/%d", &day, &month, &year) == 3 &&
            day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 1900);
}

// Helper function to process ANZ transaction line
static void process_anz_transaction_line(const char *line, ParseResult *result) {
    if (!line || !result) return;
    
    // Skip leading whitespace
    while (*line && isspace(*line)) line++;
    if (*line == '\0') return;
    
    // Parse ANZ format: DD/MM/YYYY DD/MM/YYYY NNNN DESCRIPTION... $AMOUNT $BALANCE
    // Example: "07/07/2025 02/07/2025 8410 SPOTIFY SYDNEY $19.99 $2,147.91"
    
    char *line_copy = safe_strdup(line);
    if (!line_copy) return;
    
    char processed_date[15], transaction_date[15], card[10];
    
    // Parse the first three fixed-width fields
    int parsed = sscanf(line_copy, "%14s %14s %9s", processed_date, transaction_date, card);
    if (parsed < 3) {
        free(line_copy);
        return;
    }
    
    // Use a simpler approach: split the line and work backwards from known positions
    // Format: "07/07/2025 02/07/2025 8410 SPOTIFY SYDNEY $19.99 $2,147.91"
    
    // Find the last space before the balance amount (last $)
    const char *last_dollar = strrchr(line, '$');
    if (!last_dollar) {
        free(line_copy);
        return;
    }
    
    // Find the start of the balance amount by going backwards to previous space
    const char *balance_start = last_dollar;
    while (balance_start > line && !isspace(*(balance_start - 1))) balance_start--;
    
    // Find the transaction amount (second to last $ field)
    const char *txn_amount_end = balance_start - 1;
    while (txn_amount_end > line && isspace(*txn_amount_end)) txn_amount_end--; // skip spaces
    
    if (*txn_amount_end != '$' && txn_amount_end > line) {
        // Search backwards for the $
        while (txn_amount_end > line && *txn_amount_end != '$') txn_amount_end--;
    }
    
    if (*txn_amount_end != '$') {
        free(line_copy);
        return;
    }
    
    // Find start of transaction amount
    const char *txn_amount_start = txn_amount_end;
    while (txn_amount_start > line && !isspace(*(txn_amount_start - 1))) txn_amount_start--;
    
    // Extract transaction amount
    size_t amount_len = (txn_amount_end - txn_amount_start) + 1;
    while (txn_amount_start + amount_len < balance_start && !isspace(txn_amount_start[amount_len])) amount_len++;
    
    char amount_str[50] = "";
    if (amount_len > 0 && amount_len < 49) {
        strncpy(amount_str, txn_amount_start, amount_len);
        amount_str[amount_len] = '\0';
        
        // Trim trailing whitespace
        char *trim = amount_str + strlen(amount_str) - 1;
        while (trim > amount_str && isspace(*trim)) {
            *trim = '\0';
            trim--;
        }
    }
    
    // Find where description starts (after the card field)
    const char *p = line;
    int field_count = 0;
    while (*p && field_count < 3) {
        // Skip current field
        while (*p && !isspace(*p)) p++;
        // Skip whitespace to next field  
        while (*p && isspace(*p)) p++;
        field_count++;
    }
    
    // Extract description (from after card field to start of transaction amount)
    char description[500] = "";
    if (p < txn_amount_start) {
        size_t desc_len = txn_amount_start - p;
        if (desc_len > 0 && desc_len < 499) {
            strncpy(description, p, desc_len);
            description[desc_len] = '\0';
            
            // Trim trailing whitespace
            char *end = description + strlen(description) - 1;
            while (end > description && isspace(*end)) {
                *end = '\0';
                end--;
            }
        }
    }
    
    // Parse transaction amount
    double debit, credit;
    if (parse_anz_amount(amount_str, &debit, &credit)) {
        char *iso_date = parse_anz_date(processed_date);
        if (iso_date) {
            // Debug logging
            log_message(LOG_INFO, "ANZ: Parsed transaction - Date: %s, Desc: '%s', Amount: '%s', Debit: %.2f, Credit: %.2f", 
                       iso_date, description, amount_str, debit, credit);
            
            // Add transaction date info if different from processed date
            if (strcmp(processed_date, transaction_date) != 0) {
                char full_desc[600];
                snprintf(full_desc, sizeof(full_desc), "%s [Txn Date: %s]", description, transaction_date);
                add_transaction_to_result(result, iso_date, full_desc, debit, credit, NULL);
            } else {
                add_transaction_to_result(result, iso_date, description, debit, credit, NULL);
            }
            safe_free((void**)&iso_date);
        }
    } else {
        log_message(LOG_WARN, "ANZ: Failed to parse amount '%s' for transaction '%s'", amount_str, description);
    }
    
    free(line_copy);
}

ParseResult* parse_anz_statement(const char *content, const char *correspondent __attribute__((unused))) {
    if (!content) {
        log_message(LOG_ERROR, "ANZ parser: No content provided");
        return NULL;
    }
    
    log_message(LOG_INFO, "Parsing ANZ statement...");
    
    ParseResult *result = create_parse_result();
    if (!result) return NULL;
    
    // Extract account number
    result->account_number = extract_anz_account_number(content);
    if (result->account_number) {
        log_message(LOG_INFO, "ANZ account number: %s", result->account_number);
    }
    
    // Parse transactions from table format
    char *content_copy = safe_strdup(content);
    if (!content_copy) {
        set_parse_result_error(result, "Failed to copy content for parsing");
        return result;
    }
    
    char *line = strtok(content_copy, "\n\r");
    
    while (line) {
        // Check if this line is a transaction
        if (is_anz_transaction_line(line)) {
            process_anz_transaction_line(line, result);
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    free(content_copy);
    
    log_message(LOG_INFO, "ANZ parser: Extracted %zu transactions", result->transaction_count);
    return result;
}
