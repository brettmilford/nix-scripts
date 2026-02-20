# AI-Powered PDF Parser Implementation Specification

## Overview

This specification outlines the implementation of an AI-powered PDF parsing system for bank statements. The system will support multiple AI providers and use structured prompts to extract transaction data from PDF bank statements into JSON format.

## 1. Configuration Structure

### Parser Configuration
```toml
[parsers]
anz = { method = "content" }
cba = { method = "ai", provider = "anthropic" }

[ai_providers]
anthropic = { api_key_env = "ANTHROPIC_API_KEY", base_url = "https://api.anthropic.com", model = "claude-3-5-sonnet-20241022" }
openrouter = { api_key_env = "OPENROUTER_API_KEY", base_url = "https://openrouter.ai/api/v1", model = "anthropic/claude-3.5-sonnet" }
llamacpp = { base_url = "http://localhost:8080", model = "llama-3.1-8b-instruct" }
```

## 2. Architecture Components

### AI Service Layer
- **Purpose**: Handle prompts, PDF encoding, response parsing
- **Responsibilities**:
  - Construct system and user prompts (hardcoded per bank type)
  - Handle PDF base64 encoding for API calls
  - Parse JSON responses back to ParseResult struct
  - Pass prepared data to provider-specific functions

### Provider Layer
- **Purpose**: Provider-specific API implementations
- **Functions**: 
  - `anthropic_call_api()`
  - `openrouter_call_api()`
  - `llamacpp_call_api()`
- **Requirements**: Only providers with direct PDF support allowed

### JSON Processing
- **Library**: cJSON for direct parsing
- **Validation**: Strict schema validation before processing
- **Error Handling**: Fail-fast approach with detailed error messages

## 3. JSON Schema for CBA

```json
{
  "account_number": "06 4144 10181166",
  "statement_period": "1 May 2025 - 31 Oct 2025", 
  "transactions": [
    {
      "date": "2025-06-30",
      "description": "Salary JUMP OPERATIONS",
      "debit": null,
      "credit": 29517.75,
      "balance": 31076.40
    }
  ]
}
```

### Field Requirements
- **Date**: ISO format (YYYY-MM-DD)
- **Debit/Credit**: Non-negative numbers, no currency symbols, no commas, decimal places allowed
- **Description**: String, trimmed
- **Balance**: Non-negative number following same format as debit/credit

## 4. Prompts (Hardcoded)

### CBA System Prompt
```
You are a bank statement parser. Extract transaction data accurately from PDF bank statements.
```

### CBA User Prompt
```
Extract all transactions from this CBA bank statement PDF. Return JSON with: account_number, statement_period, and transactions array. Each transaction must have: date (YYYY-MM-DD), description, debit (null or amount), credit (null or amount), balance.
```

## 5. Error Handling and Retry Logic

### Retry Policy
- **Maximum retries**: 3 attempts
- **Backoff**: Exponential backoff between retries
- **Rate limiting**: Provider-specific delays to respect API limits

### Response Validation
- Validate JSON schema matches expected structure
- Validate date format (ISO YYYY-MM-DD)
- Validate amounts are non-negative, no currency symbols/commas
- Validate required fields exist

### Fallback Behavior
- **No fallbacks**: Error out with useful information and stop processing
- **Error messages**: Include provider, model, validation failures, HTTP status codes

## 6. Testing Infrastructure

### CMake/CTest Integration
```cmake
enable_testing()

add_executable(test_ai_service tests/test_ai_service.c)
target_link_libraries(test_ai_service statement-processor-lib cjson curl)

add_test(NAME test_ai_service COMMAND test_ai_service)
```

### Test Structure
```
tests/
├── fixtures/
│   ├── cba_statement.pdf
│   ├── anz_statement.pdf
│   ├── cba_api_response.json
│   └── anz_api_response.json
├── mocks/
│   └── ai_service_mock.c
└── test_ai_service.c
```

### Test Execution
- Tests run as part of `nix build` process
- Use fixtures and mocks to avoid API costs
- Integration tests verify PDF → JSON → Spreadsheet pipeline

## 7. Dependencies

### New Dependencies
- **libcurl**: HTTP requests to AI providers
- **cjson**: JSON parsing and validation

### Build System Updates
- Update CMakeLists.txt to include new dependencies
- Update flake.nix to provide libcurl and cjson
- Integrate tests with build process

## 8. Implementation Flow

1. **Parser Registry**: Route CBA documents to AI-based parser
2. **AI Service**: Construct prompts and encode PDF
3. **Provider Layer**: Make API-specific HTTP calls
4. **Response Processing**: Parse and validate JSON response
5. **Integration**: Convert to ParseResult struct
6. **Output**: Generate spreadsheet with extracted transactions

## 9. Function Signatures

```c
typedef struct {
    char *provider;     // "anthropic", "openrouter", "llamacpp"
    char *model;        // "claude-3-5-sonnet-20241022"
    char *api_key;      // From env var
    char *base_url;     // Provider endpoint
} AIServiceConfig;

// Main AI service function
ParseResult* ai_service_parse_pdf(const char *pdf_path, AIServiceConfig *config);

// Provider-specific implementations
int anthropic_call_api(const char *pdf_base64, const char *system_prompt, 
                       const char *user_prompt, AIServiceConfig *config, char **response);
int openrouter_call_api(const char *pdf_base64, const char *system_prompt, 
                        const char *user_prompt, AIServiceConfig *config, char **response);
int llamacpp_call_api(const char *pdf_base64, const char *system_prompt, 
                      const char *user_prompt, AIServiceConfig *config, char **response);

// JSON validation
int validate_cba_json_response(const char *json_str);
ParseResult* parse_cba_json_to_result(const char *json_str);
```

## 10. Success Criteria

- CBA statements parsed with 100% transaction accuracy
- All amounts match actual PDF values (e.g., $29,517.75 for Jump Operations)
- Proper debit/credit classification
- Robust error handling with informative messages
- Test suite passes without making real API calls
- Integration with existing spreadsheet output format