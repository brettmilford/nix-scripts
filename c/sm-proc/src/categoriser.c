#include "categoriser.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void categorise_transaction(Transaction *transaction, const Config *config) {
    if (!transaction || !config) return;
    
    // If transaction already has a category, don't override it
    if (transaction->category) return;
    
    // If no description, use default category
    if (!transaction->description) {
        transaction->category = safe_strdup(config->default_category);
        return;
    }
    
    // Try to match against each category rule (first match wins)
    for (size_t i = 0; i < config->rule_count; i++) {
        if (!config->rules[i].compiled_regex) continue;
        
        // Create match data for PCRE2
        pcre2_match_data_8 *match_data = pcre2_match_data_create_from_pattern_8(
            config->rules[i].compiled_regex, NULL);
        
        if (!match_data) {
            log_message(LOG_WARN, "Failed to create match data for pattern: %s", 
                       config->rules[i].pattern);
            continue;
        }
        
        // Perform the regex match
        int match_result = pcre2_match_8(
            config->rules[i].compiled_regex,
            (PCRE2_SPTR8)transaction->description,
            strlen(transaction->description),
            0,                    // Start offset
            0,                    // Options
            match_data,
            NULL                  // Match context
        );
        
        pcre2_match_data_free_8(match_data);
        
        // Check if we got a match
        if (match_result >= 0) {
            // Found a match - assign this category
            transaction->category = safe_strdup(config->rules[i].category);
            if (!transaction->category) {
                log_message(LOG_ERROR, "Failed to allocate memory for category assignment");
                transaction->category = safe_strdup(config->default_category);
            }
            return;
        } else if (match_result != PCRE2_ERROR_NOMATCH) {
            // Some other error occurred
            PCRE2_UCHAR8 buffer[256];
            pcre2_get_error_message_8(match_result, buffer, sizeof(buffer));
            log_message(LOG_WARN, "Regex match error for pattern '%s': %s", 
                       config->rules[i].pattern, (char*)buffer);
        }
        // PCRE2_ERROR_NOMATCH is expected for non-matching patterns, continue to next rule
    }
    
    // No rules matched, use default category
    transaction->category = safe_strdup(config->default_category);
    if (!transaction->category) {
        log_message(LOG_ERROR, "Failed to allocate memory for default category");
    }
}

void categorise_all_transactions(Transaction *transactions, size_t count, const Config *config) {
    if (!transactions || count == 0 || !config) return;
    
    size_t categorised_count = 0;
    size_t default_count = 0;
    
    for (size_t i = 0; i < count; i++) {
        categorise_transaction(&transactions[i], config);
        
        // Count categorisation results
        if (transactions[i].category) {
            if (strcmp(transactions[i].category, config->default_category) == 0) {
                default_count++;
            } else {
                categorised_count++;
            }
        }
    }
    
    log_message(LOG_INFO, "Categorisation complete: %zu categorised, %zu default, %zu total", 
               categorised_count, default_count, count);
}

const char* get_transaction_category(const Transaction *transaction) {
    if (!transaction) return NULL;
    return transaction->category;
}

int is_transaction_categorised(const Transaction *transaction, const Config *config) {
    if (!transaction || !config) return 0;
    
    // If no category assigned, it's not categorised
    if (!transaction->category) return 0;
    
    // If it has the default category, it's not specifically categorised
    if (strcmp(transaction->category, config->default_category) == 0) return 0;
    
    // Has a specific category
    return 1;
}

void print_categorisation_stats(const Transaction *transactions, size_t count, const Config *config) {
    if (!transactions || count == 0 || !config) return;
    
    size_t total_categorised = 0;
    size_t total_default = 0;
    
    // Count by category
    for (size_t i = 0; i < count; i++) {
        if (transactions[i].category) {
            if (strcmp(transactions[i].category, config->default_category) == 0) {
                total_default++;
            } else {
                total_categorised++;
            }
        }
    }
    
    log_message(LOG_INFO, "Categorisation Statistics:");
    log_message(LOG_INFO, "  Total transactions: %zu", count);
    log_message(LOG_INFO, "  Categorised: %zu (%.1f%%)", 
               total_categorised, 
               count > 0 ? (total_categorised * 100.0 / count) : 0.0);
    log_message(LOG_INFO, "  Default category: %zu (%.1f%%)", 
               total_default,
               count > 0 ? (total_default * 100.0 / count) : 0.0);
               
    // Count by specific categories
    for (size_t rule_idx = 0; rule_idx < config->rule_count; rule_idx++) {
        size_t category_count = 0;
        const char *category_name = config->rules[rule_idx].category;
        
        for (size_t trans_idx = 0; trans_idx < count; trans_idx++) {
            if (transactions[trans_idx].category && 
                strcmp(transactions[trans_idx].category, category_name) == 0) {
                category_count++;
            }
        }
        
        if (category_count > 0) {
            log_message(LOG_INFO, "  %s: %zu", category_name, category_count);
        }
    }
}
