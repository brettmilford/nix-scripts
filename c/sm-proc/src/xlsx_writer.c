#include "xlsx_writer.h"
#include "utils.h"
#include <xlsxwriter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Generate filename based on date range (format: exp_report-YYYY-MM-DD-YYYY-MM-DD.xlsx)
char* generate_xlsx_filename(const char *date_from, const char *date_to, const char *output_dir) {
    if (!date_from || !date_to) return NULL;
    
    size_t dir_len = output_dir ? strlen(output_dir) : 0;
    size_t filename_len = dir_len + 50; // Enough for path + filename
    
    char *filename = malloc(filename_len);
    if (!filename) return NULL;
    
    if (output_dir && dir_len > 0) {
        snprintf(filename, filename_len, "%s/exp_report-%s-%s.xlsx", 
                output_dir, date_from, date_to);
    } else {
        snprintf(filename, filename_len, "exp_report-%s-%s.xlsx", 
                date_from, date_to);
    }
    
    return filename;
}

// Check if file exists and prompt for overwrite
bool prompt_file_overwrite(const char *filename) {
    if (!filename) return false;
    
    // Check if file exists
    if (access(filename, F_OK) != 0) {
        return true; // File doesn't exist, okay to create
    }
    
    // File exists, prompt user
    printf("File %s already exists. Overwrite? (y/n): ", filename);
    fflush(stdout);
    
    char response[10];
    if (fgets(response, sizeof(response), stdin) != NULL) {
        if (response[0] == 'y' || response[0] == 'Y') {
            return true;
        }
    }
    
    return false;
}

// Calculate statistics from transactions
XLSXStats calculate_xlsx_stats(Transaction *transactions, size_t count) {
    XLSXStats stats = {0};
    
    for (size_t i = 0; i < count; i++) {
        stats.total_transactions++;
        stats.total_debit += transactions[i].debit;
        stats.total_credit += transactions[i].credit;
        
        if (transactions[i].category && 
            strcmp(transactions[i].category, "Uncategorized") != 0) {
            stats.categorised_count++;
        } else {
            stats.uncategorised_count++;
        }
    }
    
    stats.net_amount = stats.total_credit - stats.total_debit;
    return stats;
}

// Create formatted XLSX report
int create_xlsx_report(
    const char *filename,
    Transaction *transactions,
    TransactionMetadata *metadata,
    size_t transaction_count,
    const char *date_from __attribute__((unused)),
    const char *date_to __attribute__((unused)),
    const char *paperless_url
) {
    if (!filename || !transactions) {
        log_message(LOG_ERROR, "XLSX writer: Invalid parameters");
        return 0;
    }
    
    log_message(LOG_INFO, "Creating XLSX file: %s", filename);
    
    // Create workbook
    lxw_workbook *workbook = workbook_new(filename);
    if (!workbook) {
        log_message(LOG_ERROR, "Failed to create XLSX workbook: %s", filename);
        return 0;
    }
    
    // Create worksheet
    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, "Transactions");
    if (!worksheet) {
        log_message(LOG_ERROR, "Failed to create worksheet");
        workbook_close(workbook);
        return 0;
    }
    
    // Create formats
    lxw_format *header_format = workbook_add_format(workbook);
    format_set_bold(header_format);
    format_set_bg_color(header_format, LXW_COLOR_GRAY);
    format_set_border(header_format, LXW_BORDER_THIN);
    format_set_align(header_format, LXW_ALIGN_CENTER);
    
    lxw_format *date_format = workbook_add_format(workbook);
    format_set_num_format(date_format, "dd/mm/yyyy");
    format_set_border(date_format, LXW_BORDER_THIN);
    
    lxw_format *currency_format = workbook_add_format(workbook);
    format_set_num_format(currency_format, "$#,##0.00");
    format_set_border(currency_format, LXW_BORDER_THIN);
    
    lxw_format *text_format = workbook_add_format(workbook);
    format_set_border(text_format, LXW_BORDER_THIN);
    format_set_text_wrap(text_format);
    
    lxw_format *url_format = workbook_add_format(workbook);
    format_set_border(url_format, LXW_BORDER_THIN);
    format_set_font_color(url_format, LXW_COLOR_BLUE);
    format_set_underline(url_format, LXW_UNDERLINE_SINGLE);
    
    // Set column widths
    worksheet_set_column(worksheet, 0, 0, 12, NULL);  // Date
    worksheet_set_column(worksheet, 1, 1, 30, NULL);  // Description
    worksheet_set_column(worksheet, 2, 2, 12, NULL);  // Debit
    worksheet_set_column(worksheet, 3, 3, 12, NULL);  // Credit
    worksheet_set_column(worksheet, 4, 4, 15, NULL);  // Category
    worksheet_set_column(worksheet, 5, 5, 8, NULL);   // Institution
    worksheet_set_column(worksheet, 6, 6, 18, NULL);  // Account
    worksheet_set_column(worksheet, 7, 7, 25, NULL);  // Document URL
    
    // Write headers
    worksheet_write_string(worksheet, 0, 0, "Date", header_format);
    worksheet_write_string(worksheet, 0, 1, "Description", header_format);
    worksheet_write_string(worksheet, 0, 2, "Debit", header_format);
    worksheet_write_string(worksheet, 0, 3, "Credit", header_format);
    worksheet_write_string(worksheet, 0, 4, "Category", header_format);
    worksheet_write_string(worksheet, 0, 5, "Institution", header_format);
    worksheet_write_string(worksheet, 0, 6, "Account Number", header_format);
    worksheet_write_string(worksheet, 0, 7, "Document URL", header_format);
    
    // Write transaction data
    log_message(LOG_INFO, "Writing %zu transactions to spreadsheet", transaction_count);
    
    for (size_t i = 0; i < transaction_count; i++) {
        Transaction *t = &transactions[i];
        int row = (int)(i + 1); // +1 for header row
        
        // Date - convert ISO format to Excel date
        if (t->date) {
            int year, month, day;
            if (sscanf(t->date, "%d-%d-%d", &year, &month, &day) == 3) {
                // Create Excel date value (days since 1900-01-01)
                lxw_datetime dt = {.year = year, .month = month, .day = day};
                worksheet_write_datetime(worksheet, row, 0, &dt, date_format);
            } else {
                worksheet_write_string(worksheet, row, 0, t->date, text_format);
            }
        } else {
            worksheet_write_string(worksheet, row, 0, "", text_format);
        }
        
        // Description
        worksheet_write_string(worksheet, row, 1, 
                              t->description ? t->description : "", text_format);
        
        // Debit amount
        if (t->debit > 0.0) {
            worksheet_write_number(worksheet, row, 2, t->debit, currency_format);
        } else {
            worksheet_write_string(worksheet, row, 2, "", currency_format);
        }
        
        // Credit amount  
        if (t->credit > 0.0) {
            worksheet_write_number(worksheet, row, 3, t->credit, currency_format);
        } else {
            worksheet_write_string(worksheet, row, 3, "", currency_format);
        }
        
        // Category
        worksheet_write_string(worksheet, row, 4, 
                              t->category ? t->category : "Uncategorized", text_format);
        
        // Institution (from metadata)
        if (metadata && metadata[i].institution) {
            worksheet_write_string(worksheet, row, 5, metadata[i].institution, text_format);
        } else {
            worksheet_write_string(worksheet, row, 5, "Unknown", text_format);
        }
        
        // Account Number (from metadata)
        if (metadata && metadata[i].account_number) {
            worksheet_write_string(worksheet, row, 6, metadata[i].account_number, text_format);
        } else {
            worksheet_write_string(worksheet, row, 6, "", text_format);
        }
        
        // Document URL (with real document ID)
        if (paperless_url && metadata && metadata[i].document_id > 0) {
            char doc_url[500];
            snprintf(doc_url, sizeof(doc_url), "%s/documents/%d", paperless_url, metadata[i].document_id);
            worksheet_write_url(worksheet, row, 7, doc_url, url_format);
        } else {
            worksheet_write_string(worksheet, row, 7, "", text_format);
        }
    }
    
    // Add summary section
    int summary_row = (int)(transaction_count + 3);
    XLSXStats stats = calculate_xlsx_stats(transactions, transaction_count);
    
    worksheet_write_string(worksheet, summary_row, 0, "Summary", header_format);
    worksheet_write_string(worksheet, summary_row + 1, 0, "Total Transactions:", text_format);
    worksheet_write_number(worksheet, summary_row + 1, 1, (double)stats.total_transactions, text_format);
    
    worksheet_write_string(worksheet, summary_row + 2, 0, "Total Debits:", text_format);
    worksheet_write_number(worksheet, summary_row + 2, 1, stats.total_debit, currency_format);
    
    worksheet_write_string(worksheet, summary_row + 3, 0, "Total Credits:", text_format);
    worksheet_write_number(worksheet, summary_row + 3, 1, stats.total_credit, currency_format);
    
    worksheet_write_string(worksheet, summary_row + 4, 0, "Net Amount:", text_format);
    worksheet_write_number(worksheet, summary_row + 4, 1, stats.net_amount, currency_format);
    
    worksheet_write_string(worksheet, summary_row + 5, 0, "Categorised:", text_format);
    worksheet_write_number(worksheet, summary_row + 5, 1, (double)stats.categorised_count, text_format);
    
    worksheet_write_string(worksheet, summary_row + 6, 0, "Uncategorised:", text_format);
    worksheet_write_number(worksheet, summary_row + 6, 1, (double)stats.uncategorised_count, text_format);
    
    // Close workbook
    lxw_error error = workbook_close(workbook);
    if (error != LXW_NO_ERROR) {
        log_message(LOG_ERROR, "Failed to close XLSX workbook: %s", lxw_strerror(error));
        return 0;
    }
    
    log_message(LOG_INFO, "XLSX file created successfully");
    return 1;
}
