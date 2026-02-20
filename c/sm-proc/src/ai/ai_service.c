#include "ai_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <regex.h>
#include <unistd.h>
#include <math.h>

// Hardcoded prompts for CBA
const char *CBA_SYSTEM_PROMPT = "You are a bank statement parser. Extract transaction data accurately from PDF bank statements.";

const char *CBA_USER_PROMPT = "Extract all transactions from this CBA bank statement PDF. Return JSON with: account_number, statement_period, and transactions array. Each transaction must have: date (YYYY-MM-DD), description, debit (null or amount), credit (null or amount), balance.";

// HTTP response structure for libcurl
typedef struct {
    char *memory;
    size_t size;
} HTTPResponse;

// Callback for libcurl to write response data
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    HTTPResponse *mem = (HTTPResponse *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

// Configuration utilities
AIServiceConfig* create_ai_service_config(const char *provider, const char *model, 
                                         const char *api_key, const char *base_url) {
    if (!provider || !model || !base_url) {
        return NULL;
    }
    
    AIServiceConfig *config = malloc(sizeof(AIServiceConfig));
    if (!config) {
        return NULL;
    }
    
    config->provider = strdup(provider);
    config->model = strdup(model);
    config->api_key = api_key ? strdup(api_key) : NULL;
    config->base_url = strdup(base_url);
    
    if (!config->provider || !config->model || !config->base_url) {
        free_ai_service_config(config);
        return NULL;
    }
    
    return config;
}

void free_ai_service_config(AIServiceConfig *config) {
    if (!config) {
        return;
    }
    
    free(config->provider);
    free(config->model);
    free(config->api_key);
    free(config->base_url);
    free(config);
}

// Base64 encoding function
static char* base64_encode(const unsigned char *data, size_t input_length) {
    const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = 4 * ((input_length + 2) / 3);
    
    char *encoded_data = malloc(output_length + 1);
    if (!encoded_data) {
        return NULL;
    }
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }
    
    for (size_t i = 0; i < (3 - input_length % 3) % 3; i++) {
        encoded_data[output_length - 1 - i] = '=';
    }
    
    encoded_data[output_length] = '\0';
    return encoded_data;
}

// PDF to base64 conversion
char* pdf_to_base64(const char *pdf_path) {
    if (!pdf_path) {
        return NULL;
    }
    
    FILE *file = fopen(pdf_path, "rb");
    if (!file) {
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }
    
    // Read file data
    unsigned char *file_data = malloc(file_size);
    if (!file_data) {
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(file_data, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(file_data);
        return NULL;
    }
    
    // Encode to base64
    char *base64_data = base64_encode(file_data, file_size);
    free(file_data);
    
    return base64_data;
}

// Validate ISO date format (YYYY-MM-DD)
static int is_valid_iso_date(const char *date_str) {
    if (!date_str || strlen(date_str) != 10) {
        return 0;
    }
    
    regex_t regex;
    int result;
    
    // Pattern: YYYY-MM-DD
    const char *pattern = "^[0-9]{4}-[0-9]{2}-[0-9]{2}$";
    
    result = regcomp(&regex, pattern, REG_EXTENDED);
    if (result) {
        return 0;
    }
    
    result = regexec(&regex, date_str, 0, NULL, 0);
    regfree(&regex);
    
    return result == 0;
}

// Validate amount (non-negative, no currency symbols/commas)
static int is_valid_amount(double amount) {
    return amount >= 0.0;
}

// JSON validation for CBA response
int validate_cba_json_response(const char *json_str) {
    if (!json_str) {
        fprintf(stderr, "AI Service Error: JSON response is NULL\n");
        return -1;
    }
    
    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        fprintf(stderr, "AI Service Error: Failed to parse JSON response: %s\n", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "Unknown JSON error");
        return -1;
    }
    
    // Check required fields
    cJSON *account_number = cJSON_GetObjectItem(json, "account_number");
    cJSON *statement_period = cJSON_GetObjectItem(json, "statement_period");
    cJSON *transactions = cJSON_GetObjectItem(json, "transactions");
    
    if (!cJSON_IsString(account_number)) {
        fprintf(stderr, "AI Service Error: Missing or invalid 'account_number' field in JSON response\n");
        cJSON_Delete(json);
        return -1;
    }
    if (!cJSON_IsString(statement_period)) {
        fprintf(stderr, "AI Service Error: Missing or invalid 'statement_period' field in JSON response\n");
        cJSON_Delete(json);
        return -1;
    }
    if (!cJSON_IsArray(transactions)) {
        fprintf(stderr, "AI Service Error: Missing or invalid 'transactions' array in JSON response\n");
        cJSON_Delete(json);
        return -1;
    }
    
    // Validate each transaction
    cJSON *transaction = NULL;
    cJSON_ArrayForEach(transaction, transactions) {
        cJSON *date = cJSON_GetObjectItem(transaction, "date");
        cJSON *description = cJSON_GetObjectItem(transaction, "description");
        cJSON *debit = cJSON_GetObjectItem(transaction, "debit");
        cJSON *credit = cJSON_GetObjectItem(transaction, "credit");
        cJSON *balance = cJSON_GetObjectItem(transaction, "balance");
        
        // Check required fields exist
        if (!date || !description || !debit || !credit || !balance) {
            cJSON_Delete(json);
            return -1;
        }
        
        // Validate date format
        if (!cJSON_IsString(date) || !is_valid_iso_date(cJSON_GetStringValue(date))) {
            cJSON_Delete(json);
            return -1;
        }
        
        // Validate description is string
        if (!cJSON_IsString(description)) {
            cJSON_Delete(json);
            return -1;
        }
        
        // Validate amounts (either null or non-negative number)
        if (cJSON_IsNumber(debit) && !is_valid_amount(cJSON_GetNumberValue(debit))) {
            cJSON_Delete(json);
            return -1;
        }
        
        if (cJSON_IsNumber(credit) && !is_valid_amount(cJSON_GetNumberValue(credit))) {
            cJSON_Delete(json);
            return -1;
        }
        
        if (!cJSON_IsNumber(balance) || !is_valid_amount(cJSON_GetNumberValue(balance))) {
            cJSON_Delete(json);
            return -1;
        }
    }
    
    cJSON_Delete(json);
    return 0;
}

// Convert CBA JSON to ParseResult
ParseResult* parse_cba_json_to_result(const char *json_str) {
    if (!json_str || validate_cba_json_response(json_str) != 0) {
        return NULL;
    }
    
    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        return NULL;
    }
    
    ParseResult *result = malloc(sizeof(ParseResult));
    if (!result) {
        cJSON_Delete(json);
        return NULL;
    }
    
    memset(result, 0, sizeof(ParseResult));
    
    // Extract account number and statement period
    cJSON *account_number = cJSON_GetObjectItem(json, "account_number");
    cJSON *statement_period = cJSON_GetObjectItem(json, "statement_period");
    
    result->account_number = strdup(cJSON_GetStringValue(account_number));
    result->statement_period = strdup(cJSON_GetStringValue(statement_period));
    result->error_message = NULL; // Success
    
    // Extract transactions
    cJSON *transactions = cJSON_GetObjectItem(json, "transactions");
    int transaction_count = cJSON_GetArraySize(transactions);
    
    result->transactions = malloc(transaction_count * sizeof(Transaction));
    if (!result->transactions) {
        free(result->account_number);
        free(result->statement_period);
        free(result);
        cJSON_Delete(json);
        return NULL;
    }
    
    result->transaction_count = transaction_count;
    
    for (int i = 0; i < transaction_count; i++) {
        cJSON *transaction = cJSON_GetArrayItem(transactions, i);
        Transaction *t = &result->transactions[i];
        
        memset(t, 0, sizeof(Transaction));
        
        cJSON *date = cJSON_GetObjectItem(transaction, "date");
        cJSON *description = cJSON_GetObjectItem(transaction, "description");
        cJSON *debit = cJSON_GetObjectItem(transaction, "debit");
        cJSON *credit = cJSON_GetObjectItem(transaction, "credit");
        // Balance is validated but not stored in Transaction struct
        
        t->date = strdup(cJSON_GetStringValue(date));
        t->description = strdup(cJSON_GetStringValue(description));
        t->category = NULL; // Will be set by categoriser later
        
        t->debit = cJSON_IsNull(debit) ? 0.0 : cJSON_GetNumberValue(debit);
        t->credit = cJSON_IsNull(credit) ? 0.0 : cJSON_GetNumberValue(credit);
        // Note: balance is in JSON response but not stored in Transaction struct
        // as it's calculated dynamically
    }
    
    cJSON_Delete(json);
    return result;
}

// Anthropic API call implementation
int anthropic_call_api(const char *pdf_base64, const char *system_prompt, 
                       const char *user_prompt, AIServiceConfig *config, char **response) {
    if (!pdf_base64 || !system_prompt || !user_prompt || !config || !response) {
        return -1;
    }
    
    if (!config->api_key) {
        fprintf(stderr, "AI Service Error: API key required for provider '%s' with model '%s'\n", 
                config->provider, config->model);
        return -1;
    }
    
    CURL *curl;
    CURLcode res;
    HTTPResponse chunk = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    // Build JSON payload for Anthropic API
    cJSON *json = cJSON_CreateObject();
    cJSON *model = cJSON_CreateString(config->model);
    cJSON *max_tokens = cJSON_CreateNumber(4096);
    cJSON *messages = cJSON_CreateArray();
    
    cJSON *message = cJSON_CreateObject();
    cJSON *role = cJSON_CreateString("user");
    cJSON *content = cJSON_CreateArray();
    
    // Add system prompt as first message
    cJSON *system_content = cJSON_CreateObject();
    cJSON_AddStringToObject(system_content, "type", "text");
    cJSON_AddStringToObject(system_content, "text", system_prompt);
    cJSON_AddItemToArray(content, system_content);
    
    // Add user prompt
    cJSON *user_content = cJSON_CreateObject();
    cJSON_AddStringToObject(user_content, "type", "text");
    cJSON_AddStringToObject(user_content, "text", user_prompt);
    cJSON_AddItemToArray(content, user_content);
    
    // Add PDF data
    cJSON *pdf_content = cJSON_CreateObject();
    cJSON_AddStringToObject(pdf_content, "type", "document");
    cJSON *source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "type", "base64");
    cJSON_AddStringToObject(source, "media_type", "application/pdf");
    cJSON_AddStringToObject(source, "data", pdf_base64);
    cJSON_AddItemToObject(pdf_content, "source", source);
    cJSON_AddItemToArray(content, pdf_content);
    
    cJSON_AddItemToObject(message, "role", role);
    cJSON_AddItemToObject(message, "content", content);
    cJSON_AddItemToArray(messages, message);
    
    cJSON_AddItemToObject(json, "model", model);
    cJSON_AddItemToObject(json, "max_tokens", max_tokens);
    cJSON_AddItemToObject(json, "messages", messages);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    // Set up curl
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", config->api_key);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, auth_header);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/messages", config->base_url);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    
    res = curl_easy_perform(curl);
    
    // Get HTTP status code for detailed error reporting
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_string);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "AI Service Error: HTTP request failed for provider '%s' (model: %s): %s\n", 
                config->provider, config->model, curl_easy_strerror(res));
        if (chunk.memory) {
            free(chunk.memory);
        }
        return -1;
    }
    
    if (http_code >= 400) {
        fprintf(stderr, "AI Service Error: HTTP %ld error from provider '%s' (model: %s)\n", 
                http_code, config->provider, config->model);
        if (chunk.memory) {
            fprintf(stderr, "Response: %s\n", chunk.memory);
            free(chunk.memory);
        }
        return -1;
    }
    
    if (!chunk.memory) {
        fprintf(stderr, "AI Service Error: Empty response from provider '%s' (model: %s)\n", 
                config->provider, config->model);
        return -1;
    }
    
    *response = chunk.memory;
    return 0;
}

// OpenRouter API call implementation  
int openrouter_call_api(const char *pdf_base64, const char *system_prompt, 
                        const char *user_prompt, AIServiceConfig *config, char **response) {
    if (!pdf_base64 || !system_prompt || !user_prompt || !config || !response) {
        return -1;
    }
    
    if (!config->api_key) {
        return -1; // API key required for OpenRouter
    }
    
    // Similar implementation to Anthropic but with OpenRouter API format
    // For now, return error as we don't have direct PDF support confirmed
    return -1;
}

// Llama.cpp API call implementation
int llamacpp_call_api(const char *pdf_base64, const char *system_prompt, 
                      const char *user_prompt, AIServiceConfig *config, char **response) {
    if (!pdf_base64 || !system_prompt || !user_prompt || !config || !response) {
        return -1;
    }
    
    // Llama.cpp typically doesn't support direct PDF processing
    // Return error for now as per spec requirement for known good configurations
    return -1;
}

// Main AI service PDF parsing function with retry logic
ParseResult* ai_service_parse_pdf(const char *pdf_path, AIServiceConfig *config) {
    if (!pdf_path || !config) {
        return NULL;
    }
    
    // Convert PDF to base64
    char *pdf_base64 = pdf_to_base64(pdf_path);
    if (!pdf_base64) {
        return NULL;
    }
    
    char *response = NULL;
    int result = -1;
    int max_retries = 3;
    
    // Retry logic with exponential backoff
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        if (strcmp(config->provider, "anthropic") == 0) {
            result = anthropic_call_api(pdf_base64, CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, config, &response);
        } else if (strcmp(config->provider, "openrouter") == 0) {
            result = openrouter_call_api(pdf_base64, CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, config, &response);
        } else if (strcmp(config->provider, "llamacpp") == 0) {
            result = llamacpp_call_api(pdf_base64, CBA_SYSTEM_PROMPT, CBA_USER_PROMPT, config, &response);
        } else {
            break; // Unsupported provider
        }
        
        if (result == 0 && response) {
            break; // Success
        }
        
        // Clean up response if failed
        if (response) {
            free(response);
            response = NULL;
        }
        
        // Exponential backoff delay
        if (attempt < max_retries) {
            usleep(1000000 * (int)pow(2, attempt)); // 1s, 2s, 4s delays
        }
    }
    
    free(pdf_base64);
    
    if (result != 0 || !response) {
        if (response) {
            free(response);
        }
        return NULL;
    }
    
    // Parse the JSON response
    ParseResult *parse_result = parse_cba_json_to_result(response);
    free(response);
    
    return parse_result;
}
