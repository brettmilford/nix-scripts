#ifndef PARSER_H
#define PARSER_H

#include "../transaction.h"

// Parser function signature
typedef ParseResult* (*ParserFunc)(const char *content, const char *correspondent);

typedef struct {
    const char *correspondent_name;
    ParserFunc parser;
} ParserEntry;

// Parser registry functions
ParserFunc get_parser_for_correspondent(const char *correspondent);
const char** get_supported_correspondents(void);
int is_correspondent_supported(const char *correspondent);

#endif // PARSER_H
