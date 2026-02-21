# Statement Extractor - Comprehensive Technical Specification

## 1. Project Overview

**Application Name:** `statement-extractor`

**Purpose:** A Go application that downloads bank statement data from Paperless-ngx via its REST API, extracts transaction lines from statement content, categorizes transactions, and compiles them into an XLSX spreadsheet for expense reporting.

**Language:** Go

**Build System:** Go modules (via Nix flake)

---

## 2. Dependencies

### Core Libraries
- **Standard Library:** `net/http` (HTTP client), `regexp` (regex), `context` (cancellation), `log/slog` (structured logging)
- **`github.com/xuri/excelize/v2`** - XLSX file generation
- **`github.com/spf13/viper`** - Configuration management (TOML, env vars, CLI flags)
- **`github.com/spf13/cobra`** - CLI framework
- **`github.com/stretchr/testify`** - Testing framework

### Build Tools
- Go 1.21+ (for modern features like slog, errors.Join, generics)
- Go modules for dependency management

---

## 3. Architecture

### 3.1 Application Structure
Standard Go project layout with internal packages:

```
statement-extractor/
├── main.go                                # Entry point
├── go.mod                                 # Go module definition
├── go.sum                                 # Dependency checksums
├── internal/
│   ├── config/
│   │   └── config.go                      # Configuration handling
│   ├── paperless/
│   │   └── client.go                      # Paperless API client
│   ├── parser/
│   │   ├── parser.go                      # Parser interface & registry
│   │   ├── cba.go                         # Commonwealth Bank parser
│   │   └── anz.go                         # ANZ parser
│   ├── categorizer/
│   │   └── categorizer.go                 # Transaction categorization
│   ├── xlsx/
│   │   └── writer.go                      # XLSX generation
│   └── models/
│       └── transaction.go                 # Transaction data structures
├── configs/
│   └── statement-extractor.toml           # Example configuration
├── testdata/                              # Test fixtures
└── docs/
    └── spec.md                            # This specification
```

### 3.2 Core Data Types

```go
// Transaction represents a single bank transaction
type Transaction struct {
    Date        time.Time     `json:"date"`
    Description string        `json:"description"`
    Debit       *float64      `json:"debit,omitempty"`   // nil if credit transaction
    Credit      *float64      `json:"credit,omitempty"`  // nil if debit transaction
    Category    string        `json:"category"`
    Institution string        `json:"institution"`
    Account     string        `json:"account"`
    DocumentID  int          `json:"document_id"`
}

// ParseResult represents the result of parsing a bank statement
type ParseResult struct {
    AccountNumber     string        `json:"account_number"`
    StatementPeriod   string        `json:"statement_period,omitempty"` // For year resolution
    Transactions      []Transaction `json:"transactions"`
    Institution       string        `json:"institution"`
    DocumentID        int          `json:"document_id"`
}

// Parser defines the interface for bank-specific parsers
type Parser interface {
    Parse(ctx context.Context, content, correspondent string) (*ParseResult, error)
    Supports(correspondent string) bool
}
```

---

## 4. Configuration

### 4.1 Environment Variables
- **`PAPERLESS_URL`** (required) - Base URL of Paperless instance (e.g., `https://paperless.example.com`)
- **`PAPERLESS_API_KEY`** (required) - API authentication token

### 4.2 Configuration File Format (TOML)

**Location:** Specified via `--config` flag or auto-discovered as:
- `./statement-extractor.toml`
- `~/.config/statement-extractor/statement-extractor.toml`
- `/etc/statement-extractor/statement-extractor.toml`

**Format:**
```toml
# Default category for uncategorized transactions
default_category = "Uncategorized"

# Parser configuration - specifies how to process different bank statements
[parsers]
  [parsers.anz]
  method = "content"  # ANZ uses content-based text parsing
  
  [parsers.cba]
  method = "pdf"       # CBA uses PDF-based PDF parsing
  provider = "pdf-service-1"

# PDF service configuration for PDF-based parsing
[pdf_services]
  [pdf_services.pdf-service-1]
  api_key_env = "PDF_SERVICE_1_API_KEY"
  base_url = "https://api.pdf-service-1.com"
  model = "claude-3-5-sonnet-20241022"
  
  [pdf_services.pdf-service-2]
  api_key_env = "PDF_SERVICE_2_API_KEY"
  base_url = "https://pdf-service-2.pdf/api/v1"
  model = "pdf-service-1/claude-3.5-sonnet"
  
  [pdf_services.pdf-service-3]
  base_url = "http://localhost:8080"
  model = "llama-3.1-8b-instruct"

# Categorization rules (evaluated in order - first match wins)
[[categories]]
pattern = "SALARY|WAGE|JUMP OPERATIONS|DIRECT CREDIT"
category = "Income"

[[categories]]
pattern = "TRANSFER.*TO.*|TRANSFER.*FROM.*|PAYMENT.*RECEIVED"
category = "Transfer"

[[categories]]
pattern = "WOOLWORTHS|COLES|ALDI|IGA|SUPERMARKET|GROCERY"
category = "Groceries & household"

# ... (additional categorization rules)
```

**Behavior:**
- Configuration file is optional
- Environment variables override config file values
- CLI flags override both environment variables and config file
- If no config provided, all transactions get default category "Uncategorized"
- Regex patterns are case-insensitive
- First matching pattern wins
- Patterns match agpdfnst transaction Description field only

### 4.3 Application Constants

```go
const (
    // Paperless tag IDs
    TagAccountsID      = 123   // "Accounts" tag
    TagProcessedID     = 456   // "processed" tag
    
    // Paperless custom field ID
    FieldExpenseReportID = 789 // "Expense Report" field
    
    // API configuration
    APITimeoutSeconds    = 30
    APIMaxRetries       = 3
    APIRetryBackoffMS   = 1000 // Initial backoff, doubles each retry
    
    // Date format
    DateFormatISO = "2006-01-02"
)
```

---

## 5. Command-Line Interface

### 5.1 Usage
```bash
statement-extractor --date-from <YYYY-MM-DD> --date-to <YYYY-MM-DD> [--config <path>] [--output-dir <path>] [--reprocess]
```

### 5.2 Commands and Flags

```go
// Root command using Cobra
var rootCmd = &cobra.Command{
    Use:   "statement-extractor",
    Short: "Extract and categorize bank transactions from Paperless-ngx",
    Long:  `Downloads bank statements from Paperless-ngx, parses transactions, and generates XLSX reports.`,
}

// Flags
rootCmd.Flags().String("date-from", "", "Start date (ISO format: YYYY-MM-DD, inclusive)")
rootCmd.Flags().String("date-to", "", "End date (ISO format: YYYY-MM-DD, inclusive)")
rootCmd.Flags().String("config", "", "Path to configuration file")
rootCmd.Flags().StringP("output-dir", "o", ".", "Output directory (default: current directory)")
rootCmd.Flags().Bool("reprocess", false, "Include documents already tagged as 'processed'")
rootCmd.Flags().BoolP("verbose", "v", false, "Enable verbose logging")
rootCmd.Flags().Bool("dry-run", false, "Show what would be processed without making changes")
```

| Flag | Required | Description |
|------|----------|-------------|
| `--date-from` | Yes | Start date (ISO format: YYYY-MM-DD, inclusive) |
| `--date-to` | Yes | End date (ISO format: YYYY-MM-DD, inclusive) |
| `--config` | No | Path to configuration file |
| `--output-dir, -o` | No | Output directory (default: current directory) |
| `--reprocess` | No | Include documents already tagged as "processed" |
| `--verbose, -v` | No | Enable verbose logging |
| `--dry-run` | No | Show what would be processed without making changes |

### 5.3 Examples
```bash
# Basic usage with config
nix run .#statement-extractor -- --date-from 2024-01-01 --date-to 2024-01-31 --config statement-extractor.toml

# Without config (all transactions uncategorized)
statement-extractor --date-from 2024-01-01 --date-to 2024-01-31

# Custom output directory with verbose logging
statement-extractor --date-from 2024-01-01 --date-to 2024-01-31 -o ~/reports/ -v

# Dry run to see what would be processed
statement-extractor --date-from 2024-01-01 --date-to 2024-01-31 --dry-run

# Reprocess already processed documents
statement-extractor --date-from 2024-01-01 --date-to 2024-01-31 --reprocess
```

---

## 6. Paperless API Integration

### 6.1 HTTP Client Implementation

```go
// PaperlessClient handles all Paperless API interactions
type PaperlessClient struct {
    baseURL    string
    apiKey     string
    httpClient *http.Client
    logger     *slog.Logger
}

// NewPaperlessClient creates a new client with retry logic
func NewPaperlessClient(baseURL, apiKey string, logger *slog.Logger) *PaperlessClient {
    return &PaperlessClient{
        baseURL: strings.TrimSuffix(baseURL, "/"),
        apiKey:  apiKey,
        httpClient: &http.Client{
            Timeout: time.Duration(APITimeoutSeconds) * time.Second,
        },
        logger: logger,
    }
}

// makeRequest handles HTTP requests with retry logic and exponential backoff
func (c *PaperlessClient) makeRequest(ctx context.Context, method, endpoint string, body io.Reader) (*http.Response, error) {
    var lastErr error
    backoff := time.Duration(APIRetryBackoffMS) * time.Millisecond
    
    for attempt := 0; attempt <= APIMaxRetries; attempt++ {
        req, err := http.NewRequestWithContext(ctx, method, c.baseURL+endpoint, body)
        if err != nil {
            return nil, fmt.Errorf("creating request: %w", err)
        }
        
        req.Header.Set("Authorization", "Token "+c.apiKey)
        req.Header.Set("Content-Type", "application/json")
        
        resp, err := c.httpClient.Do(req)
        if err == nil {
            // Success or non-retryable error
            if resp.StatusCode < 500 {
                return resp, nil
            }
            resp.Body.Close()
            lastErr = fmt.Errorf("server error: %s", resp.Status)
        } else {
            lastErr = err
        }
        
        // Retry logic
        if attempt < APIMaxRetries {
            c.logger.Warn("Request fpdfled, retrying", 
                slog.Int("attempt", attempt+1),
                slog.Duration("backoff", backoff),
                slog.String("error", lastErr.Error()),
            )
            
            select {
            case <-time.After(backoff):
                backoff *= 2 // Exponential backoff
            case <-ctx.Done():
                return nil, ctx.Err()
            }
        }
    }
    
    return nil, fmt.Errorf("request fpdfled after %d attempts: %w", APIMaxRetries+1, lastErr)
}
```

### 6.2 API Endpoints

#### Query Documents
```
GET /api/documents/?tags__id__all={TAG_ACCOUNTS_ID}&created__date__gte={date_from}&created__date__lte={date_to}[&tags__id__all__exclude={TAG_PROCESSED_ID}]
```

**Parameters:**
- `tags__id__all={TAG_ACCOUNTS_ID}` - Filter by "Accounts" tag
- `created__date__gte={date_from}` - Documents created on or after date
- `created__date__lte={date_to}` - Documents created on or before date
- `tags__id__all__exclude={TAG_PROCESSED_ID}` - Exclude processed documents (omit if `--reprocess` flag set)
- `page=N` - Handle pagination (fetch all pages)

**Go Implementation:**
```go
type Document struct {
    ID           int    `json:"id"`
    Correspondent *int   `json:"correspondent"` // May be null
    Content      string `json:"content"`
    Created      string `json:"created"`       // YYYY-MM-DD format
}

type DocumentsResponse struct {
    Count    int        `json:"count"`
    Next     *string    `json:"next"`
    Previous *string    `json:"previous"`
    Results  []Document `json:"results"`
}

func (c *PaperlessClient) GetDocuments(ctx context.Context, dateFrom, dateTo string, reprocess bool) ([]Document, error) {
    var allDocs []Document
    nextURL := fmt.Sprintf("/api/documents/?tags__id__all=%d&created__date__gte=%s&created__date__lte=%s",
        TagAccountsID, dateFrom, dateTo)
    
    if !reprocess {
        nextURL += fmt.Sprintf("&tags__id__all__exclude=%d", TagProcessedID)
    }
    
    for nextURL != "" {
        resp, err := c.makeRequest(ctx, "GET", nextURL, nil)
        if err != nil {
            return nil, fmt.Errorf("fetching documents: %w", err)
        }
        defer resp.Body.Close()
        
        var docResp DocumentsResponse
        if err := json.NewDecoder(resp.Body).Decode(&docResp); err != nil {
            return nil, fmt.Errorf("decoding response: %w", err)
        }
        
        allDocs = append(allDocs, docResp.Results...)
        
        if docResp.Next != nil {
            nextURL = *docResp.Next
            // Extract path from full URL if necessary
            if strings.HasPrefix(nextURL, "http") {
                parsed, _ := url.Parse(nextURL)
                nextURL = parsed.Path + "?" + parsed.RawQuery
            }
        } else {
            nextURL = ""
        }
    }
    
    return allDocs, nil
}
```

#### Update Document
```
PATCH /api/documents/{id}/
```

**Request Body (JSON):**
```json
{
  "tags": [TAG_ACCOUNTS_ID, TAG_PROCESSED_ID],
  "custom_fields": [
    {
      "field": FIELD_EXPENSE_REPORT_ID,
      "value": "2024-01-01-2024-01-31"
    }
  ]
}
```

**Go Implementation:**
```go
type CustomField struct {
    Field int    `json:"field"`
    Value string `json:"value"`
}

type DocumentUpdate struct {
    Tags         []int         `json:"tags"`
    CustomFields []CustomField `json:"custom_fields"`
}

func (c *PaperlessClient) MarkDocumentProcessed(ctx context.Context, docID int, reportPeriod string) error {
    update := DocumentUpdate{
        Tags: []int{TagAccountsID, TagProcessedID},
        CustomFields: []CustomField{
            {
                Field: FieldExpenseReportID,
                Value: reportPeriod,
            },
        },
    }
    
    body, err := json.Marshal(update)
    if err != nil {
        return fmt.Errorf("marshaling update: %w", err)
    }
    
    endpoint := fmt.Sprintf("/api/documents/%d/", docID)
    resp, err := c.makeRequest(ctx, "PATCH", endpoint, bytes.NewReader(body))
    if err != nil {
        return fmt.Errorf("updating document %d: %w", docID, err)
    }
    defer resp.Body.Close()
    
    if resp.StatusCode != http.StatusOK {
        return fmt.Errorf("unexpected status: %s", resp.Status)
    }
    
    return nil
}
```

### 6.3 Error Handling

**Retry Logic:**
- Retry up to 3 times for network/transient errors (HTTP 5xx, timeouts, connection fpdflures)
- Use exponential backoff: 1s, 2s, 4s
- Log each retry attempt with structured logging

**Non-Retryable Errors:**
- HTTP 401/403 (authentication/authorization) - fpdfl immediately
- HTTP 404 (resource not found) - fpdfl immediately  
- HTTP 400 (bad request) - fpdfl immediately

**Modern Go Error Handling:**
```go
func (p *Processor) ProcessDocuments(ctx context.Context, dateFrom, dateTo string) error {
    docs, err := p.client.GetDocuments(ctx, dateFrom, dateTo, p.config.Reprocess)
    if err != nil {
        return fmt.Errorf("fetching documents: %w", err)
    }
    
    var errs []error
    var processedDocs []int
    
    for _, doc := range docs {
        result, err := p.processDocument(ctx, doc)
        if err != nil {
            errs = append(errs, fmt.Errorf("document %d: %w", doc.ID, err))
            continue
        }
        
        p.addTransactions(result.Transactions)
        processedDocs = append(processedDocs, doc.ID)
    }
    
    // Only mark documents as processed if XLSX generation succeeds
    if err := p.generateXLSX(ctx); err != nil {
        return fmt.Errorf("generating XLSX: %w", err)
    }
    
    // Mark documents as processed
    for _, docID := range processedDocs {
        if err := p.client.MarkDocumentProcessed(ctx, docID, p.reportPeriod()); err != nil {
            errs = append(errs, fmt.Errorf("marking document %d as processed: %w", docID, err))
        }
    }
    
    if len(errs) > 0 {
        p.logger.Warn("Some documents had errors", slog.Int("error_count", len(errs)))
        return errors.Join(errs...) // Go 1.20+ multiple error joining
    }
    
    return nil
}
```

---

## 7. Statement Parsing

### 7.1 Parser Architecture

#### Parser Interface
```go
// Parser defines the interface for bank-specific parsers
type Parser interface {
    Parse(ctx context.Context, content, correspondent string) (*ParseResult, error)
    Supports(correspondent string) bool
    Name() string
}

// ParserRegistry manages avpdflable parsers
type ParserRegistry struct {
    parsers []Parser
    logger  *slog.Logger
}

func NewParserRegistry(logger *slog.Logger) *ParserRegistry {
    return &ParserRegistry{
        parsers: []Parser{
            NewCBAParser(logger),
            NewANZParser(logger),
        },
        logger: logger,
    }
}

func (r *ParserRegistry) FindParser(correspondent string) Parser {
    for _, parser := range r.parsers {
        if parser.Supports(correspondent) {
            r.logger.Debug("Found parser", 
                slog.String("correspondent", correspondent),
                slog.String("parser", parser.Name()),
            )
            return parser
        }
    }
    return nil
}
```

### 7.2 Institution-Specific Parsers

#### 7.2.1 CBA (Commonwealth Bank) Parser

```go
type CBAParser struct {
    logger        *slog.Logger
    accountRegex  *regexp.Regexp
    periodRegex   *regexp.Regexp
    transRegex    *regexp.Regexp
    dateRegex     *regexp.Regexp
}

func NewCBAParser(logger *slog.Logger) *CBAParser {
    return &CBAParser{
        logger:        logger,
        accountRegex:  regexp.MustCompile(`Account Number\s+(\d{2}\s+\d{4}\s+\d+)`),
        periodRegex:   regexp.MustCompile(`Statement Period\s+(\d{1,2}\s+\w+\s+\d{4})\s+-\s+(\d{1,2}\s+\w+\s+\d{4})`),
        transRegex:    regexp.MustCompile(`^(\d{1,2})\s+(\w+)\s+(.+)$`),
        dateRegex:     regexp.MustCompile(`^\d{1,2}\s+\w+`),
    }
}

func (p *CBAParser) Supports(correspondent string) bool {
    return correspondent == "CBA" || 
           correspondent == "Commonwealth Bank" || 
           correspondent == "133"
}

func (p *CBAParser) Name() string {
    return "CBA"
}

func (p *CBAParser) Parse(ctx context.Context, content, correspondent string) (*ParseResult, error) {
    // Extract account number
    accountMatches := p.accountRegex.FindStringSubmatch(content)
    if len(accountMatches) < 2 {
        return nil, fmt.Errorf("could not find account number")
    }
    accountNumber := strings.ReplaceAll(accountMatches[1], " ", "")
    
    // Extract statement period for year resolution
    periodMatches := p.periodRegex.FindStringSubmatch(content)
    if len(periodMatches) < 3 {
        return nil, fmt.Errorf("could not find statement period")
    }
    
    startDate, err := p.parseStatementDate(periodMatches[1])
    if err != nil {
        return nil, fmt.Errorf("parsing start date: %w", err)
    }
    
    endDate, err := p.parseStatementDate(periodMatches[2])
    if err != nil {
        return nil, fmt.Errorf("parsing end date: %w", err)
    }
    
    // Parse transactions
    transactions, err := p.parseTransactions(content, startDate, endDate)
    if err != nil {
        return nil, fmt.Errorf("parsing transactions: %w", err)
    }
    
    return &ParseResult{
        AccountNumber:   accountNumber,
        StatementPeriod: fmt.Sprintf("%s - %s", periodMatches[1], periodMatches[2]),
        Transactions:    transactions,
        Institution:     "CBA",
    }, nil
}

func (p *CBAParser) parseTransactions(content string, startDate, endDate time.Time) ([]Transaction, error) {
    lines := strings.Split(content, "\n")
    var transactions []Transaction
    var currentTrans *Transaction
    
    for _, line := range lines {
        line = strings.TrimSpace(line)
        if line == "" {
            continue
        }
        
        // Check if this is a transaction start line
        if p.dateRegex.MatchString(line) {
            // Save previous transaction if exists
            if currentTrans != nil {
                transactions = append(transactions, *currentTrans)
            }
            
            // Start new transaction
            trans, err := p.parseTransactionLine(line, startDate, endDate)
            if err != nil {
                p.logger.Warn("Fpdfled to parse transaction line", 
                    slog.String("line", line),
                    slog.String("error", err.Error()),
                )
                continue
            }
            currentTrans = &trans
        } else if currentTrans != nil {
            // This is a continuation line (Card info, Value Date, Amount)
            if strings.HasPrefix(line, "Card ") {
                currentTrans.Description += " " + line
            } else if strings.HasPrefix(line, "Value Date:") {
                currentTrans.Description += " " + line
            } else if strings.Contpdfns(line, "$") {
                // This is the amount line
                if err := p.parseAmount(line, currentTrans); err != nil {
                    p.logger.Warn("Fpdfled to parse amount", 
                        slog.String("line", line),
                        slog.String("error", err.Error()),
                    )
                }
            }
        }
    }
    
    // Add the last transaction
    if currentTrans != nil {
        transactions = append(transactions, *currentTrans)
    }
    
    return transactions, nil
}

func (p *CBAParser) parseTransactionLine(line string, startDate, endDate time.Time) (Transaction, error) {
    matches := p.transRegex.FindStringSubmatch(line)
    if len(matches) < 4 {
        return Transaction{}, fmt.Errorf("invalid transaction format")
    }
    
    day, err := strconv.Atoi(matches[1])
    if err != nil {
        return Transaction{}, fmt.Errorf("invalid day: %w", err)
    }
    
    month := matches[2]
    description := strings.TrimSpace(matches[3])
    
    // Resolve year based on statement period
    date, err := p.resolveTransactionDate(day, month, startDate, endDate)
    if err != nil {
        return Transaction{}, fmt.Errorf("resolving date: %w", err)
    }
    
    return Transaction{
        Date:        date,
        Description: description,
    }, nil
}

func (p *CBAParser) resolveTransactionDate(day int, monthStr string, startDate, endDate time.Time) (time.Time, error) {
    // Parse month name to number
    monthNum, err := p.parseMonth(monthStr)
    if err != nil {
        return time.Time{}, err
    }
    
    // Determine year based on statement period
    var year int
    if monthNum < int(startDate.Month()) {
        // Transaction is in the following year
        year = endDate.Year()
    } else {
        year = startDate.Year()
    }
    
    return time.Date(year, time.Month(monthNum), day, 0, 0, 0, 0, time.UTC), nil
}

func (p *CBAParser) parseMonth(monthStr string) (int, error) {
    months := map[string]int{
        "Jan": 1, "Feb": 2, "Mar": 3, "Apr": 4,
        "May": 5, "Jun": 6, "Jul": 7, "Aug": 8,
        "Sep": 9, "Oct": 10, "Nov": 11, "Dec": 12,
    }
    
    if month, ok := months[monthStr]; ok {
        return month, nil
    }
    
    return 0, fmt.Errorf("unknown month: %s", monthStr)
}

func (p *CBAParser) parseAmount(line string, trans *Transaction) error {
    // CBA format: amount ( = debit, $ prefix = credit
    if strings.Contpdfns(line, " ( ") {
        // Debit transaction
        re := regexp.MustCompile(`([\d,]+\.\d{2})\s+\(`)
        matches := re.FindStringSubmatch(line)
        if len(matches) > 1 {
            amount, err := p.parseFloat(matches[1])
            if err != nil {
                return err
            }
            trans.Debit = &amount
        }
    } else if strings.Contpdfns(line, "$ $") {
        // Credit transaction
        re := regexp.MustCompile(`([\d,]+\.\d{2})\s+\$\s+\$`)
        matches := re.FindStringSubmatch(line)
        if len(matches) > 1 {
            amount, err := p.parseFloat(matches[1])
            if err != nil {
                return err
            }
            trans.Credit = &amount
        }
    }
    
    return nil
}

func (p *CBAParser) parseFloat(s string) (float64, error) {
    // Remove commas and parse
    cleaned := strings.ReplaceAll(s, ",", "")
    return strconv.ParseFloat(cleaned, 64)
}
```

#### 7.2.2 ANZ Parser

```go
type ANZParser struct {
    logger       *slog.Logger
    accountRegex *regexp.Regexp
    transRegex   *regexp.Regexp
}

func NewANZParser(logger *slog.Logger) *ANZParser {
    return &ANZParser{
        logger:       logger,
        accountRegex: regexp.MustCompile(`ACCOUNT NUMBER:\s*([0-9-]+)`),
        transRegex:   regexp.MustCompile(`^(\d{2}/\d{2}/\d{4})\s+(\d{2}/\d{2}/\d{4})\s+(\d+)\s+(.+)\s+\$([0-9,]+\.\d{2})(CR)?\s+\$([0-9,]+\.\d{2})CR`),
    }
}

func (p *ANZParser) Supports(correspondent string) bool {
    return correspondent == "ANZ" || correspondent == "11"
}

func (p *ANZParser) Name() string {
    return "ANZ"
}

func (p *ANZParser) Parse(ctx context.Context, content, correspondent string) (*ParseResult, error) {
    // Extract account number
    accountMatches := p.accountRegex.FindStringSubmatch(content)
    if len(accountMatches) < 2 {
        return nil, fmt.Errorf("could not find account number")
    }
    
    // Parse transactions
    transactions, err := p.parseTransactions(content)
    if err != nil {
        return nil, fmt.Errorf("parsing transactions: %w", err)
    }
    
    return &ParseResult{
        AccountNumber: accountMatches[1],
        Transactions:  transactions,
        Institution:   "ANZ",
    }, nil
}

func (p *ANZParser) parseTransactions(content string) ([]Transaction, error) {
    lines := strings.Split(content, "\n")
    var transactions []Transaction
    
    for _, line := range lines {
        line = strings.TrimSpace(line)
        if !p.transRegex.MatchString(line) {
            continue
        }
        
        matches := p.transRegex.FindStringSubmatch(line)
        if len(matches) < 8 {
            continue
        }
        
        processedDate, err := time.Parse("02/01/2006", matches[1])
        if err != nil {
            p.logger.Warn("Fpdfled to parse processed date", 
                slog.String("date", matches[1]),
                slog.String("error", err.Error()),
            )
            continue
        }
        
        transactionDate, err := time.Parse("02/01/2006", matches[2])
        if err != nil {
            p.logger.Warn("Fpdfled to parse transaction date", 
                slog.String("date", matches[2]),
                slog.String("error", err.Error()),
            )
            continue
        }
        
        description := strings.TrimSpace(matches[4])
        
        // Add transaction date to description if different from processed date
        if !transactionDate.Equal(processedDate) {
            description += fmt.Sprintf(" (Transaction Date: %s)", transactionDate.Format("2006-01-02"))
        }
        
        // Parse amount
        amountStr := strings.ReplaceAll(matches[5], ",", "")
        amount, err := strconv.ParseFloat(amountStr, 64)
        if err != nil {
            p.logger.Warn("Fpdfled to parse amount", 
                slog.String("amount", matches[5]),
                slog.String("error", err.Error()),
            )
            continue
        }
        
        transaction := Transaction{
            Date:        processedDate,
            Description: description,
        }
        
        // Check if this is a credit (CR suffix) or debit
        if matches[6] == "CR" {
            transaction.Credit = &amount
        } else {
            transaction.Debit = &amount
        }
        
        transactions = append(transactions, transaction)
    }
    
    return transactions, nil
}
```

### 7.3 Unknown Institution Handling

```go
func (r *ParserRegistry) ParseDocument(ctx context.Context, doc Document) (*ParseResult, error) {
    correspondent := "Unknown"
    if doc.Correspondent != nil {
        // In real implementation, you'd lookup correspondent name by ID
        correspondent = fmt.Sprintf("Correspondent_%d", *doc.Correspondent)
    }
    
    parser := r.FindParser(correspondent)
    if parser == nil {
        r.logger.Warn("Unknown institution, skipping document",
            slog.String("correspondent", correspondent),
            slog.Int("document_id", doc.ID),
        )
        return nil, fmt.Errorf("no parser avpdflable for correspondent: %s", correspondent)
    }
    
    result, err := parser.Parse(ctx, doc.Content, correspondent)
    if err != nil {
        r.logger.Warn("Fpdfled to parse document",
            slog.Int("document_id", doc.ID),
            slog.String("correspondent", correspondent),
            slog.String("parser", parser.Name()),
            slog.String("error", err.Error()),
        )
        return nil, fmt.Errorf("parsing fpdfled: %w", err)
    }
    
    result.DocumentID = doc.ID
    return result, nil
}
```

---

## 8. Transaction Categorization

### 8.1 Categorizer Implementation

```go
type Rule struct {
    Pattern  *regexp.Regexp
    Category string
}

type Categorizer struct {
    rules           []Rule
    defaultCategory string
    logger          *slog.Logger
}

func NewCategorizer(config *Config, logger *slog.Logger) (*Categorizer, error) {
    var rules []Rule
    
    for _, category := range config.Categories {
        // Compile regex with case-insensitive flag
        pattern, err := regexp.Compile("(?i)" + category.Pattern)
        if err != nil {
            logger.Error("Fpdfled to compile regex pattern",
                slog.String("pattern", category.Pattern),
                slog.String("category", category.Category),
                slog.String("error", err.Error()),
            )
            continue // Skip invalid patterns but continue with others
        }
        
        rules = append(rules, Rule{
            Pattern:  pattern,
            Category: category.Category,
        })
    }
    
    return &Categorizer{
        rules:           rules,
        defaultCategory: config.DefaultCategory,
        logger:          logger,
    }, nil
}

func (c *Categorizer) Categorize(transaction *Transaction) {
    for _, rule := range c.rules {
        if rule.Pattern.MatchString(transaction.Description) {
            transaction.Category = rule.Category
            c.logger.Debug("Transaction categorized",
                slog.String("description", transaction.Description),
                slog.String("category", rule.Category),
                slog.String("pattern", rule.Pattern.String()),
            )
            return
        }
    }
    
    // No match found, use default category
    transaction.Category = c.defaultCategory
    c.logger.Debug("Transaction used default category",
        slog.String("description", transaction.Description),
        slog.String("category", c.defaultCategory),
    )
}

func (c *Categorizer) CategorizeAll(transactions []Transaction) {
    for i := range transactions {
        c.Categorize(&transactions[i])
    }
}
```

### 8.2 Example Categorization

```go
// Example usage
description := "COLES SUPERMARKET SPRINGVALE"
pattern := regexp.MustCompile(`(?i)GROCERY|SUPERMARKET|COLES|WOOLWORTHS|IGA`)
if pattern.MatchString(description) {
    category = "Groceries"
}

description = "NETFLIX.COM"
// No pattern matches, so category = "Uncategorized" (default)
```

---

## 9. XLSX Generation

### 9.1 XLSX Writer Implementation

```go
type XLSXWriter struct {
    logger *slog.Logger
}

func NewXLSXWriter(logger *slog.Logger) *XLSXWriter {
    return &XLSXWriter{logger: logger}
}

func (w *XLSXWriter) WriteTransactions(ctx context.Context, transactions []Transaction, outputPath string, paperlessURL string) error {
    f := excelize.NewFile()
    defer f.Close()
    
    sheetName := "Transactions"
    index, err := f.NewSheet(sheetName)
    if err != nil {
        return fmt.Errorf("creating sheet: %w", err)
    }
    f.SetActiveSheet(index)
    
    // Set headers
    headers := []string{
        "Date", "Description", "Debit", "Credit", 
        "Category", "Institution", "Account", "Document URL",
    }
    
    for i, header := range headers {
        cell := fmt.Sprintf("%s1", string(rune('A'+i)))
        if err := f.SetCellValue(sheetName, cell, header); err != nil {
            return fmt.Errorf("setting header %s: %w", header, err)
        }
    }
    
    // Apply bold formatting to headers
    style, err := f.NewStyle(&excelize.Style{
        Font: &excelize.Font{Bold: true},
    })
    if err != nil {
        return fmt.Errorf("creating header style: %w", err)
    }
    
    if err := f.SetRowStyle(sheetName, 1, 1, style); err != nil {
        return fmt.Errorf("applying header style: %w", err)
    }
    
    // Sort transactions by date
    sort.Slice(transactions, func(i, j int) bool {
        if transactions[i].Date.Equal(transactions[j].Date) {
            if transactions[i].Institution == transactions[j].Institution {
                return transactions[i].Description < transactions[j].Description
            }
            return transactions[i].Institution < transactions[j].Institution
        }
        return transactions[i].Date.Before(transactions[j].Date)
    })
    
    // Add transaction data
    for i, trans := range transactions {
        row := i + 2 // Start from row 2 (after headers)
        
        // Date (ISO format)
        if err := f.SetCellValue(sheetName, fmt.Sprintf("A%d", row), trans.Date.Format(DateFormatISO)); err != nil {
            return fmt.Errorf("setting date for row %d: %w", row, err)
        }
        
        // Description
        if err := f.SetCellValue(sheetName, fmt.Sprintf("B%d", row), trans.Description); err != nil {
            return fmt.Errorf("setting description for row %d: %w", row, err)
        }
        
        // Debit (only if not nil)
        if trans.Debit != nil {
            if err := f.SetCellValue(sheetName, fmt.Sprintf("C%d", row), *trans.Debit); err != nil {
                return fmt.Errorf("setting debit for row %d: %w", row, err)
            }
        }
        
        // Credit (only if not nil)  
        if trans.Credit != nil {
            if err := f.SetCellValue(sheetName, fmt.Sprintf("D%d", row), *trans.Credit); err != nil {
                return fmt.Errorf("setting credit for row %d: %w", row, err)
            }
        }
        
        // Category
        if err := f.SetCellValue(sheetName, fmt.Sprintf("E%d", row), trans.Category); err != nil {
            return fmt.Errorf("setting category for row %d: %w", row, err)
        }
        
        // Institution
        if err := f.SetCellValue(sheetName, fmt.Sprintf("F%d", row), trans.Institution); err != nil {
            return fmt.Errorf("setting institution for row %d: %w", row, err)
        }
        
        // Account
        if err := f.SetCellValue(sheetName, fmt.Sprintf("G%d", row), trans.Account); err != nil {
            return fmt.Errorf("setting account for row %d: %w", row, err)
        }
        
        // Document URL
        docURL := fmt.Sprintf("%s/documents/%d", strings.TrimSuffix(paperlessURL, "/"), trans.DocumentID)
        if err := f.SetCellValue(sheetName, fmt.Sprintf("H%d", row), docURL); err != nil {
            return fmt.Errorf("setting document URL for row %d: %w", row, err)
        }
    }
    
    // Set number format for Debit and Credit columns (2 decimal places)
    numStyle, err := f.NewStyle(&excelize.Style{
        NumFmt: 2, // 0.00 format
    })
    if err != nil {
        return fmt.Errorf("creating number style: %w", err)
    }
    
    lastRow := len(transactions) + 1
    if err := f.SetColStyle(sheetName, "C", numStyle); err != nil {
        return fmt.Errorf("setting debit column style: %w", err)
    }
    if err := f.SetColStyle(sheetName, "D", numStyle); err != nil {
        return fmt.Errorf("setting credit column style: %w", err)
    }
    
    // Auto-size columns
    for i := 0; i < len(headers); i++ {
        col := string(rune('A' + i))
        if err := f.SetColWidth(sheetName, col, col, 15); err != nil {
            w.logger.Warn("Fpdfled to set column width", 
                slog.String("column", col),
                slog.String("error", err.Error()),
            )
        }
    }
    
    // Check for existing file and handle overwrite
    if err := w.handleExistingFile(outputPath); err != nil {
        return err
    }
    
    // Save file
    if err := f.SaveAs(outputPath); err != nil {
        return fmt.Errorf("saving file: %w", err)
    }
    
    w.logger.Info("XLSX file generated successfully",
        slog.String("path", outputPath),
        slog.Int("transactions", len(transactions)),
    )
    
    return nil
}

func (w *XLSXWriter) handleExistingFile(path string) error {
    if _, err := os.Stat(path); err != nil {
        if os.IsNotExist(err) {
            return nil // File doesn't exist, proceed
        }
        return fmt.Errorf("checking file existence: %w", err)
    }
    
    // File exists, prompt for overwrite
    fmt.Printf("File %s already exists. Overwrite? (y/n): ", path)
    
    var response string
    for {
        if _, err := fmt.Scanln(&response); err != nil {
            return fmt.Errorf("reading user input: %w", err)
        }
        
        response = strings.ToLower(strings.TrimSpace(response))
        if response == "y" || response == "yes" {
            return nil // Proceed with overwrite
        } else if response == "n" || response == "no" {
            return fmt.Errorf("user chose not to overwrite existing file")
        }
        
        fmt.Print("Please enter y or n: ")
    }
}
```

### 9.2 File Naming and Location

```go
func GenerateFilename(dateFrom, dateTo string) string {
    return fmt.Sprintf("exp_report-%s-%s.xlsx", dateFrom, dateTo)
}

func GenerateOutputPath(outputDir, dateFrom, dateTo string) string {
    filename := GenerateFilename(dateFrom, dateTo)
    return filepath.Join(outputDir, filename)
}
```

---

## 10. Testing

### 10.1 Test Structure

```go
// internal/parser/cba_test.go
package parser

import (
    "context"
    "log/slog"
    "os"
    "testing"
    "time"
    
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestCBAParser_Parse(t *testing.T) {
    logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelDebug}))
    parser := NewCBAParser(logger)
    
    testCases := []struct {
        name           string
        content        string
        expectedAccount string
        expectedTxns   int
        expectError    bool
    }{
        {
            name:           "valid CBA statement",
            content:        loadTestData(t, "cba_statement.txt"),
            expectedAccount: "06414410181166",
            expectedTxns:   5,
            expectError:    false,
        },
        {
            name:        "invalid content",
            content:     "invalid content",
            expectError: true,
        },
    }
    
    for _, tc := range testCases {
        t.Run(tc.name, func(t *testing.T) {
            ctx := context.Background()
            result, err := parser.Parse(ctx, tc.content, "CBA")
            
            if tc.expectError {
                assert.Error(t, err)
                return
            }
            
            require.NoError(t, err)
            assert.Equal(t, tc.expectedAccount, result.AccountNumber)
            assert.Len(t, result.Transactions, tc.expectedTxns)
            assert.Equal(t, "CBA", result.Institution)
            
            // Validate transaction structure
            for _, txn := range result.Transactions {
                assert.False(t, txn.Date.IsZero(), "Transaction date should not be zero")
                assert.NotEmpty(t, txn.Description, "Transaction description should not be empty")
                
                // Either debit or credit should be set, but not both
                hasDebit := txn.Debit != nil
                hasCredit := txn.Credit != nil
                assert.True(t, hasDebit != hasCredit, "Transaction should have either debit or credit, not both")
            }
        })
    }
}

func TestCBAParser_Supports(t *testing.T) {
    logger := slog.New(slog.NewTextHandler(os.Stdout, nil))
    parser := NewCBAParser(logger)
    
    testCases := []struct {
        correspondent string
        expected      bool
    }{
        {"CBA", true},
        {"Commonwealth Bank", true},
        {"133", true},
        {"ANZ", false},
        {"Unknown", false},
    }
    
    for _, tc := range testCases {
        t.Run(tc.correspondent, func(t *testing.T) {
            assert.Equal(t, tc.expected, parser.Supports(tc.correspondent))
        })
    }
}

func loadTestData(t *testing.T, filename string) string {
    t.Helper()
    content, err := os.ReadFile(filepath.Join("../../testdata", filename))
    require.NoError(t, err)
    return string(content)
}
```

### 10.2 Integration Tests

```go
// integration_test.go
//go:build integration

package mpdfn

import (
    "context"
    "log/slog"
    "os"
    "testing"
    
    "github.com/stretchr/testify/assert"
    "github.com/stretchr/testify/require"
)

func TestEndToEndProcessing(t *testing.T) {
    if testing.Short() {
        t.Skip("Skipping integration test in short mode")
    }
    
    // Require environment variables for integration tests
    paperlessURL := os.Getenv("PAPERLESS_URL")
    paperlessAPIKey := os.Getenv("PAPERLESS_API_KEY")
    
    if paperlessURL == "" || paperlessAPIKey == "" {
        t.Skip("PAPERLESS_URL and PAPERLESS_API_KEY must be set for integration tests")
    }
    
    logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelDebug}))
    
    // Test the full processing pipeline
    processor := &Processor{
        client:      NewPaperlessClient(paperlessURL, paperlessAPIKey, logger),
        registry:    NewParserRegistry(logger),
        categorizer: NewCategorizer(&Config{DefaultCategory: "Test"}, logger),
        xlsxWriter:  NewXLSXWriter(logger),
        logger:      logger,
    }
    
    ctx := context.Background()
    err := processor.ProcessDocuments(ctx, "2024-01-01", "2024-01-31")
    
    // Should not error (but may have warnings for unparseable documents)
    assert.NoError(t, err)
}
```

### 10.3 Benchmark Tests

```go
func BenchmarkCBAParser_Parse(b *testing.B) {
    logger := slog.New(slog.NewTextHandler(io.Discard, nil))
    parser := NewCBAParser(logger)
    content := loadTestData(b, "large_cba_statement.txt")
    
    b.ResetTimer()
    
    for i := 0; i < b.N; i++ {
        _, err := parser.Parse(context.Background(), content, "CBA")
        if err != nil {
            b.Fatal(err)
        }
    }
}
```

---

## 11. Logging and Monitoring

### 11.1 Structured Logging with slog

```go
// Initialize logger with JSON output for production
func initLogger(verbose bool) *slog.Logger {
    level := slog.LevelInfo
    if verbose {
        level = slog.LevelDebug
    }
    
    opts := &slog.HandlerOptions{
        Level: level,
        AddSource: verbose,
    }
    
    var handler slog.Handler
    if isProduction() {
        handler = slog.NewJSONHandler(os.Stdout, opts)
    } else {
        handler = slog.NewTextHandler(os.Stdout, opts)
    }
    
    return slog.New(handler)
}

// Usage throughout application
logger.Info("Starting document processing",
    slog.String("date_from", dateFrom),
    slog.String("date_to", dateTo),
    slog.Bool("reprocess", reprocess),
)

logger.Error("Fpdfled to process document",
    slog.Int("document_id", doc.ID),
    slog.String("correspondent", correspondent),
    slog.String("error", err.Error()),
)
```

### 11.2 Progress Tracking

```go
type ProgressTracker struct {
    total     int
    processed int
    errors    int
    logger    *slog.Logger
}

func (p *ProgressTracker) Start(total int) {
    p.total = total
    p.logger.Info("Processing started",
        slog.Int("total_documents", total),
    )
}

func (p *ProgressTracker) Success(docID int) {
    p.processed++
    if p.processed%10 == 0 || p.processed == p.total {
        p.logger.Info("Processing progress",
            slog.Int("processed", p.processed),
            slog.Int("total", p.total),
            slog.Int("errors", p.errors),
            slog.Float64("percent", float64(p.processed)/float64(p.total)*100),
        )
    }
}

func (p *ProgressTracker) Error(docID int, err error) {
    p.errors++
    p.logger.Warn("Document processing fpdfled",
        slog.Int("document_id", docID),
        slog.String("error", err.Error()),
    )
}

func (p *ProgressTracker) Complete() {
    p.logger.Info("Processing completed",
        slog.Int("total", p.total),
        slog.Int("successful", p.processed),
        slog.Int("errors", p.errors),
        slog.Float64("success_rate", float64(p.processed)/float64(p.total)*100),
    )
}
```

---

## 12. Build and Deployment

### 12.1 Go Module Configuration

```go
// go.mod
module github.com/example/statement-extractor

go 1.21

require (
    github.com/spf13/cobra v1.8.0
    github.com/spf13/viper v1.18.2
    github.com/stretchr/testify v1.8.4
    github.com/xuri/excelize/v2 v2.8.1
)

require (
    // ... indirect dependencies
)
```

### 12.2 Nix Flake Integration

The application integrates with the existing Nix flake structure:

```bash
# Build the application
nix build .#statement-extractor

# Run with arguments
nix run .#statement-extractor -- --date-from 2024-01-01 --date-to 2024-01-31

# Development environment
nix develop .#go

# Run tests
nix flake check  # Includes Go tests
```

### 12.3 CI/CD Considerations

```go
// Makefile for local development
.PHONY: test build clean lint

test:
	go test -v ./...

test-integration:
	go test -v -tags=integration ./...

build:
	go build -o bin/statement-extractor .

clean:
	rm -rf bin/

lint:
	go vet ./...
	go fmt ./...

benchmark:
	go test -bench=. ./...
```

---

## 13. Configuration Examples

### 13.1 Development Configuration

```toml
# configs/statement-extractor.toml
default_category = "Uncategorized"

[parsers]
  [parsers.anz]
  method = "content"
  
  [parsers.cba]
  method = "pdf"
  provider = "pdf-service-1"

[pdf_services]
  [pdf_services.pdf-service-1]
  api_key_env = "PDF_SERVICE_1_API_KEY"
  base_url = "https://api.pdf-service-1.com"
  model = "claude-3-5-sonnet-20241022"

# Simple categorization rules for testing
[[categories]]
pattern = "SALARY|WAGE|DIRECT CREDIT"
category = "Income"

[[categories]]
pattern = "COLES|WOOLWORTHS|SUPERMARKET"
category = "Groceries"

[[categories]]
pattern = "TRANSFER.*TO.*|PAYMENT.*RECEIVED"
category = "Transfer"
```

### 13.2 Production Configuration

```toml
# /etc/statement-extractor/statement-extractor.toml
default_category = "Uncategorized"

[parsers]
  [parsers.anz]
  method = "content"
  
  [parsers.cba] 
  method = "pdf"
  provider = "pdf-service-1"

[pdf_services]
  [pdf_services.pdf-service-1]
  api_key_env = "PDF_SERVICE_1_API_KEY"
  base_url = "https://api.pdf-service-1.com"
  model = "claude-3-5-sonnet-20241022"
  
  [pdf_services.pdf-service-2]
  api_key_env = "PDF_SERVICE_2_API_KEY" 
  base_url = "https://pdf-service-2.pdf/api/v1"
  model = "pdf-service-1/claude-3.5-sonnet"

# Comprehensive categorization rules
[[categories]]
pattern = "SALARY|WAGE|JUMP OPERATIONS|DIRECT CREDIT"
category = "Income"

[[categories]]
pattern = "TRANSFER.*TO.*|TRANSFER.*FROM.*|PAYMENT.*RECEIVED"
category = "Transfer"

# ... (all other categories from the original config)
```

---

## 14. Error Handling and Recovery

### 14.1 Graceful Error Handling

```go
func (p *Processor) ProcessDocuments(ctx context.Context, dateFrom, dateTo string) error {
    // Context cancellation support
    if err := ctx.Err(); err != nil {
        return fmt.Errorf("context cancelled: %w", err)
    }
    
    docs, err := p.client.GetDocuments(ctx, dateFrom, dateTo, p.config.Reprocess)
    if err != nil {
        return fmt.Errorf("fetching documents: %w", err)
    }
    
    if len(docs) == 0 {
        p.logger.Info("No documents found for processing",
            slog.String("date_from", dateFrom),
            slog.String("date_to", dateTo),
        )
        return nil
    }
    
    p.progress.Start(len(docs))
    defer p.progress.Complete()
    
    var allTransactions []Transaction
    var processedDocs []int
    var errs []error
    
    for _, doc := range docs {
        select {
        case <-ctx.Done():
            return ctx.Err()
        default:
        }
        
        result, err := p.processDocument(ctx, doc)
        if err != nil {
            errs = append(errs, fmt.Errorf("document %d: %w", doc.ID, err))
            p.progress.Error(doc.ID, err)
            continue
        }
        
        allTransactions = append(allTransactions, result.Transactions...)
        processedDocs = append(processedDocs, doc.ID)
        p.progress.Success(doc.ID)
    }
    
    if len(allTransactions) == 0 {
        return fmt.Errorf("no transactions were successfully processed")
    }
    
    // Categorize transactions
    p.categorizer.CategorizeAll(allTransactions)
    
    // Generate XLSX - only if we have transactions
    outputPath := GenerateOutputPath(p.config.OutputDir, dateFrom, dateTo)
    if err := p.xlsxWriter.WriteTransactions(ctx, allTransactions, outputPath, p.config.PaperlessURL); err != nil {
        return fmt.Errorf("generating XLSX file: %w", err)
    }
    
    // Only mark documents as processed if XLSX generation succeeded
    var markingErrs []error
    for _, docID := range processedDocs {
        if err := p.client.MarkDocumentProcessed(ctx, docID, p.reportPeriod(dateFrom, dateTo)); err != nil {
            markingErrs = append(markingErrs, fmt.Errorf("marking document %d as processed: %w", docID, err))
        }
    }
    
    // Combine all errors
    allErrs := append(errs, markingErrs...)
    if len(allErrs) > 0 {
        if len(allErrs) == len(docs) {
            // All documents fpdfled
            return errors.Join(allErrs...)
        } else {
            // Partial success - log warnings but don't fpdfl
            p.logger.Warn("Some operations had errors",
                slog.Int("total_errors", len(allErrs)),
                slog.Int("successful_transactions", len(allTransactions)),
            )
        }
    }
    
    return nil
}
```

### 14.2 Recovery Strategies

```go
// Retry wrapper for critical operations
func withRetry[T any](ctx context.Context, operation func(ctx context.Context) (T, error), maxRetries int, logger *slog.Logger) (T, error) {
    var zero T
    var lastErr error
    
    for attempt := 0; attempt <= maxRetries; attempt++ {
        result, err := operation(ctx)
        if err == nil {
            return result, nil
        }
        
        lastErr = err
        
        if attempt < maxRetries {
            backoff := time.Duration(attempt+1) * time.Second
            logger.Warn("Operation fpdfled, retrying",
                slog.Int("attempt", attempt+1),
                slog.Int("max_retries", maxRetries),
                slog.Duration("backoff", backoff),
                slog.String("error", err.Error()),
            )
            
            select {
            case <-time.After(backoff):
            case <-ctx.Done():
                return zero, ctx.Err()
            }
        }
    }
    
    return zero, fmt.Errorf("operation fpdfled after %d attempts: %w", maxRetries+1, lastErr)
}
```

---

This specification provides a comprehensive guide for implementing the statement-extractor application in idiomatic Go, incorporating modern language features, proper error handling, structured logging, and robust testing practices while mpdfntpdfning the same functional requirements as the original C implementation.