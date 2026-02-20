#include "paperless_api.h"
#include "utils.h"
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Response data structure for curl
struct APIResponse {
    char *memory;
    size_t size;
};

// Callback function for curl to write response data
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    struct APIResponse *response = (struct APIResponse *)userp;
    size_t realsize = size * nmemb;
    char *ptr = realloc(response->memory, response->size + realsize + 1);
    
    if (ptr == NULL) {
        log_message(LOG_ERROR, "Not enough memory for API response (realloc failed)");
        return 0;
    }
    
    response->memory = ptr;
    memcpy(&(response->memory[response->size]), contents, realsize);
    response->size += realsize;
    response->memory[response->size] = '\0';
    
    return realsize;
}

// Initialize curl with common settings
static CURL* init_curl_handle(const char *api_key) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_message(LOG_ERROR, "Failed to initialize curl");
        return NULL;
    }
    
    // Set headers
    struct curl_slist *headers = NULL;
    char auth_header[500];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Token %s", api_key);
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "User-Agent: statement-processor/1.0.0");
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    
    return curl;
}

// Make HTTP request with retry logic
static struct APIResponse* make_api_request(const char *url, const char *api_key, int max_retries) {
    struct APIResponse *response = malloc(sizeof(struct APIResponse));
    if (!response) return NULL;
    
    response->memory = malloc(1);
    response->size = 0;
    
    for (int retry = 0; retry <= max_retries; retry++) {
        if (retry > 0) {
            int delay = 1 << (retry - 1); // Exponential backoff: 1, 2, 4, 8 seconds
            log_message(LOG_ERROR, "Retry %d/%d in %d seconds...", retry, max_retries, delay);
            sleep(delay);
        }
        
        CURL *curl = init_curl_handle(api_key);
        if (!curl) {
            free(response->memory);
            free(response);
            return NULL;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            
            if (response_code == 200) {
                curl_easy_cleanup(curl);
                return response;
            } else {
                log_message(LOG_ERROR, "HTTP error %ld for URL: %s", response_code, url);
                curl_easy_cleanup(curl);
                if (response_code >= 400 && response_code < 500) {
                    // Client error - don't retry
                    break;
                }
            }
        } else {
            log_message(LOG_ERROR, "Failed to connect to Paperless API: %s", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
        }
    }
    
    free(response->memory);
    free(response);
    return NULL;
}

// Parse documents from JSON response
static PaperlessDocument* parse_documents_json(const char *json_str, int *count) {
    *count = 0;
    
    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        log_message(LOG_ERROR, "Failed to parse JSON response");
        return NULL;
    }
    
    cJSON *results = cJSON_GetObjectItemCaseSensitive(json, "results");
    if (!cJSON_IsArray(results)) {
        log_message(LOG_ERROR, "JSON response missing 'results' array");
        cJSON_Delete(json);
        return NULL;
    }
    
    int doc_count = cJSON_GetArraySize(results);
    if (doc_count == 0) {
        cJSON_Delete(json);
        return NULL;
    }
    
    PaperlessDocument *documents = malloc(doc_count * sizeof(PaperlessDocument));
    if (!documents) {
        cJSON_Delete(json);
        return NULL;
    }
    
    int valid_docs = 0;
    for (int i = 0; i < doc_count; i++) {
        cJSON *doc = cJSON_GetArrayItem(results, i);
        if (!doc) continue;
        
        cJSON *id = cJSON_GetObjectItemCaseSensitive(doc, "id");
        cJSON *correspondent = cJSON_GetObjectItemCaseSensitive(doc, "correspondent");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(doc, "content");
        cJSON *created = cJSON_GetObjectItemCaseSensitive(doc, "created");
        
        if (!cJSON_IsNumber(id) || !cJSON_IsString(content) || !cJSON_IsString(created)) {
            log_message(LOG_WARN, "Document %d missing required fields, skipping", i);
            continue;
        }
        
        documents[valid_docs].id = id->valueint;
        documents[valid_docs].content = safe_strdup(content->valuestring);
        documents[valid_docs].created_date = safe_strdup(created->valuestring);
        
        // Correspondent can be null, a number (ID), string, or object with name
        if (cJSON_IsString(correspondent)) {
            // Direct string correspondent name
            documents[valid_docs].correspondent = safe_strdup(correspondent->valuestring);
        } else if (cJSON_IsObject(correspondent)) {
            // Correspondent object with name field
            cJSON *name = cJSON_GetObjectItemCaseSensitive(correspondent, "name");
            if (cJSON_IsString(name)) {
                documents[valid_docs].correspondent = safe_strdup(name->valuestring);
            } else {
                log_message(LOG_INFO, "Document %d has correspondent object without name field", id->valueint);
                documents[valid_docs].correspondent = NULL;
            }
        } else if (cJSON_IsNumber(correspondent)) {
            // Correspondent is an ID - convert to string for parser lookup
            int correspondent_id = correspondent->valueint;
            char id_str[20];
            snprintf(id_str, sizeof(id_str), "%d", correspondent_id);
            documents[valid_docs].correspondent = safe_strdup(id_str);
            log_message(LOG_INFO, "Document %d has correspondent ID %d", 
                       id->valueint, correspondent_id);
        } else if (cJSON_IsNull(correspondent)) {
            log_message(LOG_INFO, "Document %d has null correspondent", id->valueint);
            documents[valid_docs].correspondent = NULL;
        } else {
            log_message(LOG_INFO, "Document %d has unexpected correspondent type", id->valueint);
            documents[valid_docs].correspondent = NULL;
        }
        
        valid_docs++;
    }
    
    *count = valid_docs;
    cJSON_Delete(json);
    return documents;
}

// Query documents from Paperless API with pagination
PaperlessDocument* query_documents(const char *date_from, const char *date_to, int reprocess, int *total_count) {
    if (!date_from || !date_to || !total_count) {
        log_message(LOG_ERROR, "Invalid parameters for document query");
        *total_count = 0;
        return NULL;
    }
    
    const char *paperless_url = getenv("PAPERLESS_URL");
    const char *paperless_api_key = getenv("PAPERLESS_API_KEY");
    
    if (!paperless_url || !paperless_api_key) {
        log_message(LOG_ERROR, "Missing PAPERLESS_URL or PAPERLESS_API_KEY environment variables");
        *total_count = 0;
        return NULL;
    }
    
    log_message(LOG_INFO, "Querying Paperless API...");
    
    // Build initial API URL with Accounts tag filtering (ID 14)
    char base_url[1000];
    if (reprocess) {
        snprintf(base_url, sizeof(base_url),
                "%s/api/documents/?tags__id__all=14&created__date__gte=%s&created__date__lte=%s&ordering=created",
                paperless_url, date_from, date_to);
    } else {
        // Filter by Accounts tag and exclude processed documents  
        snprintf(base_url, sizeof(base_url),
                "%s/api/documents/?tags__id__all=14&created__date__gte=%s&created__date__lte=%s&ordering=created",
                paperless_url, date_from, date_to);
    }
    
    PaperlessDocument *all_documents = NULL;
    int total_docs = 0;
    int page = 1;
    
    while (1) {
        char page_url[1200];
        snprintf(page_url, sizeof(page_url), "%s&page=%d", base_url, page);
        
        log_message(LOG_INFO, "Fetching page %d...", page);
        struct APIResponse *response = make_api_request(page_url, paperless_api_key, 3);
        
        if (!response) {
            log_message(LOG_ERROR, "Failed to fetch page %d", page);
            break;
        }
        
        int page_count;
        PaperlessDocument *page_documents = parse_documents_json(response->memory, &page_count);
        
        free(response->memory);
        free(response);
        
        if (page_count == 0) {
            // No more documents
            if (page_documents) free(page_documents);
            break;
        }
        
        // Expand the documents array
        all_documents = realloc(all_documents, (total_docs + page_count) * sizeof(PaperlessDocument));
        if (!all_documents) {
            log_message(LOG_ERROR, "Memory allocation failed for documents");
            *total_count = 0;
            return NULL;
        }
        
        // Copy page documents to main array
        memcpy(&all_documents[total_docs], page_documents, page_count * sizeof(PaperlessDocument));
        total_docs += page_count;
        
        free(page_documents);
        
        // Check if we should continue pagination
        if (page_count < 25) { // Paperless default page size is 25
            break;
        }
        
        page++;
    }
    
    *total_count = total_docs;
    log_message(LOG_INFO, "Found %d matching documents", total_docs);
    return all_documents;
}

// Update document tags after processing
int update_document_tags(int document_id, const char *date_from, const char *date_to) {
    const char *paperless_url = getenv("PAPERLESS_URL");
    const char *paperless_api_key = getenv("PAPERLESS_API_KEY");
    
    if (!paperless_url || !paperless_api_key) {
        log_message(LOG_ERROR, "Missing environment variables for document tagging");
        return 0;
    }
    
    // First, get the current document to retrieve existing tags
    char doc_url[500];
    snprintf(doc_url, sizeof(doc_url), "%s/api/documents/%d/", paperless_url, document_id);
    
    struct APIResponse *get_response = make_api_request(doc_url, paperless_api_key, 2);
    if (!get_response) {
        log_message(LOG_ERROR, "Failed to get document %d for tagging", document_id);
        return 0;
    }
    
    // Parse existing tags
    cJSON *doc_json = cJSON_Parse(get_response->memory);
    if (!doc_json) {
        log_message(LOG_ERROR, "Failed to parse document %d JSON for tagging", document_id);
        free(get_response->memory);
        free(get_response);
        return 0;
    }
    
    cJSON *tags_array = cJSON_GetObjectItemCaseSensitive(doc_json, "tags");
    if (!cJSON_IsArray(tags_array)) {
        log_message(LOG_ERROR, "Document %d has no tags array", document_id);
        cJSON_Delete(doc_json);
        free(get_response->memory);
        free(get_response);
        return 0;
    }
    
    // Check if document already has processed tag (assuming tag ID 15 for "processed")
    int has_processed_tag = 0;
    cJSON *tag = NULL;
    cJSON_ArrayForEach(tag, tags_array) {
        if (cJSON_IsNumber(tag) && tag->valueint == 15) {
            has_processed_tag = 1;
            break;
        }
    }
    
    if (has_processed_tag) {
        log_message(LOG_INFO, "Document %d already has processed tag", document_id);
        cJSON_Delete(doc_json);
        free(get_response->memory);
        free(get_response);
        return 1;
    }
    
    // Add processed tag (ID 15) to existing tags
    cJSON_AddItemToArray(tags_array, cJSON_CreateNumber(15));
    
    // Create PATCH request JSON
    cJSON *patch_json = cJSON_CreateObject();
    cJSON *new_tags = cJSON_Duplicate(tags_array, 1);
    cJSON_AddItemToObject(patch_json, "tags", new_tags);
    
    char *json_string = cJSON_Print(patch_json);
    
    // Make PATCH request
    CURL *curl = init_curl_handle(paperless_api_key);
    if (!curl) {
        log_message(LOG_ERROR, "Failed to initialize curl for tagging");
        free(json_string);
        cJSON_Delete(patch_json);
        cJSON_Delete(doc_json);
        free(get_response->memory);
        free(get_response);
        return 0;
    }
    
    struct APIResponse patch_response = {0};
    patch_response.memory = malloc(1);
    patch_response.size = 0;
    
    // Set up proper headers for PATCH request
    struct curl_slist *patch_headers = NULL;
    char auth_header[500];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Token %s", paperless_api_key);
    
    patch_headers = curl_slist_append(patch_headers, "Content-Type: application/json");
    patch_headers = curl_slist_append(patch_headers, auth_header);
    patch_headers = curl_slist_append(patch_headers, "User-Agent: statement-processor/1.0.0");
    
    curl_easy_setopt(curl, CURLOPT_URL, doc_url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(json_string));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, patch_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &patch_response);
    
    CURLcode res = curl_easy_perform(curl);
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    int success = (res == CURLE_OK && response_code == 200);
    
    if (success) {
        log_message(LOG_INFO, "Tagged document %d as processed (period %s to %s)", 
                   document_id, date_from, date_to);
    } else {
        log_message(LOG_ERROR, "Failed to tag document %d: HTTP %ld", document_id, response_code);
        if (patch_response.memory && patch_response.size > 0) {
            log_message(LOG_ERROR, "Response: %s", patch_response.memory);
        }
    }
    
    // Cleanup
    curl_slist_free_all(patch_headers);
    curl_easy_cleanup(curl);
    free(patch_response.memory);
    free(json_string);
    cJSON_Delete(patch_json);
    cJSON_Delete(doc_json);
    free(get_response->memory);
    free(get_response);
    
    return success;
}

// Free a paperless document structure
void free_paperless_document(PaperlessDocument *doc) {
    if (!doc) return;
    
    safe_free((void**)&doc->correspondent);
    safe_free((void**)&doc->content);
    safe_free((void**)&doc->created_date);
}
