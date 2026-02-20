#ifndef ANZ_PARSER_H
#define ANZ_PARSER_H

#include "../transaction.h"

// ANZ parser
ParseResult* parse_anz_statement(const char *content, const char *correspondent);

#endif // ANZ_PARSER_H
