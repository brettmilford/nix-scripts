#ifndef CBA_PARSER_H
#define CBA_PARSER_H

#include "../transaction.h"
#include "../config.h"

// CBA (Commonwealth Bank) parser
ParseResult* parse_cba_statement(const char *content, const char *correspondent);

// Extended CBA parser with document ID for AI-powered PDF processing
ParseResult* parse_cba_statement_with_id(const char *content, const char *correspondent, int document_id);

// Set configuration for CBA parser
void set_cba_parser_config(Config *config);

#endif // CBA_PARSER_H
