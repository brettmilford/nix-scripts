#include "parser.h"
#include "cba_parser.h"
#include "anz_parser.h"
#include "../utils.h"
#include <string.h>

// Parser registry - matches correspondent IDs and names to parser functions
static ParserEntry parser_registry[] = {
    // Commonwealth Bank IDs and names
    {"133", parse_cba_statement},           // Your Paperless correspondent ID
    {"CBA", parse_cba_statement},
    {"Commonwealth Bank", parse_cba_statement},
    
    // ANZ IDs and names  
    {"11", parse_anz_statement},            // Your Paperless correspondent ID
    {"ANZ", parse_anz_statement},
    {"ANZ Bank", parse_anz_statement},
    
    {NULL, NULL} // Sentinel to mark end of array
};

ParserFunc get_parser_for_correspondent(const char *correspondent) {
    if (!correspondent) {
        log_message(LOG_WARN, "No correspondent provided for parser lookup");
        return NULL;
    }
    
    // Search for matching parser
    for (int i = 0; parser_registry[i].correspondent_name != NULL; i++) {
        if (strcasecmp(correspondent, parser_registry[i].correspondent_name) == 0) {
            log_message(LOG_INFO, "Found parser for correspondent: %s", correspondent);
            return parser_registry[i].parser;
        }
    }
    
    // No parser found
    log_message(LOG_WARN, "Unknown institution: %s, no parser available", correspondent);
    return NULL;
}

const char** get_supported_correspondents(void) {
    static const char* correspondents[10]; // Adjust size as needed
    int count = 0;
    
    // Extract unique correspondent names
    for (int i = 0; parser_registry[i].correspondent_name != NULL && count < 9; i++) {
        correspondents[count] = parser_registry[i].correspondent_name;
        count++;
    }
    correspondents[count] = NULL; // NULL terminate
    
    return correspondents;
}

int is_correspondent_supported(const char *correspondent) {
    return get_parser_for_correspondent(correspondent) != NULL;
}
