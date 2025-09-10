use clap::Parser;
use reqwest::Client;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::env;
use anyhow::{Result, Context, anyhow};
use rust_xlsxwriter::{Workbook, Format, Color, Formula, Url};

#[derive(Parser)]
#[command(
    name = "paperless-to-spreadsheet",
    about = "Queries paperless api to produce a excel workbook of tax documents.",
    long_about = "Queries paperless-ngx API to produce an Excel workbook of tax documents.\n\nEnvironment Variables:\n  PAPERLESS_URL      Paperless-ngx base URL (e.g., https://paperless.example.com)\n  PAPERLESS_API_KEY  API token for authentication\n\nExample:\n  paperless-to-spreadsheet 2025"
)]
struct Args {
    /// Financial Year (e.g., 2025)
    financial_year: Option<String>,
}

#[derive(Debug, Deserialize)]
struct CustomField {
    id: u32,
    name: String,
    data_type: String,
}

#[derive(Debug, Deserialize)]
struct CustomFieldsResponse {
    results: Vec<CustomField>,
}

#[derive(Debug, Deserialize)]
struct Tag {
    id: u32,
    name: String,
}

#[derive(Debug, Deserialize)]
struct TagsResponse {
    results: Vec<Tag>,
}

#[derive(Debug, Deserialize)]
struct Correspondent {
    id: u32,
    name: String,
}

#[derive(Debug, Deserialize)]
struct CorrespondentsResponse {
    results: Vec<Correspondent>,
    next: Option<String>,
}

#[derive(Debug, Deserialize)]
struct DocumentType {
    id: u32,
    name: String,
}

#[derive(Debug, Deserialize)]
struct DocumentTypesResponse {
    results: Vec<DocumentType>,
    next: Option<String>,
}

#[derive(Debug, Deserialize)]
struct DocumentCustomField {
    field: u32,
    value: Value,
}

#[derive(Debug, Deserialize)]
struct Document {
    id: u32,
    title: String,
    created_date: String,
    correspondent: Option<u32>,
    document_type: Option<u32>,
    tags: Vec<u32>,
    custom_fields: Vec<DocumentCustomField>,
}

#[derive(Debug, Deserialize)]
struct DocumentsResponse {
    results: Vec<Document>,
    next: Option<String>,
}

#[derive(Debug, Serialize)]
struct ShareLinkRequest {
    document: u32,
    expiry_date: String,
}

#[derive(Debug, Deserialize)]
struct ShareLinkResponse {
    slug: String,
}

#[derive(Debug)]
struct InvoiceData {
    title: String,
    counterparty: String,
    date: String,
    doc_type: String,
    amount: String,
    currency: String,
    notes: String,
    share_link: String,
    link: String,
}

fn parse_amount_and_currency(raw_amount: &str) -> (String, String) {
    let trimmed = raw_amount.trim();

    // Check if the string starts with a 3-letter currency code
    if trimmed.len() >= 3 {
        let potential_currency = &trimmed[0..3];

        // Check if it's all alphabetic (currency code)
        if potential_currency.chars().all(|c| c.is_alphabetic()) {
            let currency = potential_currency.to_uppercase();
            let amount = &trimmed[3..];
            return (currency, amount.to_string());
        }
    }

    // No currency code found, default to AUD
    ("AUD".to_string(), trimmed.to_string())
}

fn get_expiry_date() -> String {
    use std::time::{SystemTime, UNIX_EPOCH, Duration};

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();

    let thirty_days_later = now + (30 * 24 * 60 * 60); // 30 days in seconds

    // Convert to ISO 8601 format (YYYY-MM-DD)
    let datetime = std::time::UNIX_EPOCH + Duration::from_secs(thirty_days_later);
    let datetime: chrono::DateTime<chrono::Utc> = datetime.into();
    datetime.format("%Y-%m-%d").to_string()
}

async fn create_share_link(
    client: &Client,
    base_url: &str,
    api_key: &str,
    document_id: u32,
) -> Result<String> {
    let url = format!("{}/api/share_links/", base_url);

    let request_body = ShareLinkRequest {
        document: document_id,
        expiry_date: get_expiry_date(),
    };

    let response = client
        .post(&url)
        .header("Authorization", format!("Token {}", api_key))
        .json(&request_body)
        .send()
        .await
        .context("Failed to create share link")?;

    if !response.status().is_success() {
        return Err(anyhow!("Failed to create share link for document {}: {}", document_id, response.status()));
    }

    let share_response: ShareLinkResponse = response
        .json()
        .await
        .context("Failed to parse share link response")?;

    Ok(format!("{}/share/{}", base_url, share_response.slug))
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();

    // Check if financial year was provided
    let fy = match args.financial_year {
        Some(year) => year,
        None => {
            eprintln!("Error: Financial year is required\n");
            eprintln!("Queries paperless-ngx API to produce an Excel workbook of tax documents.\n");
            eprintln!("USAGE:");
            eprintln!("    paperless-to-spreadsheet <FINANCIAL_YEAR>\n");
            eprintln!("ARGS:");
            eprintln!("    <FINANCIAL_YEAR>    Financial Year (e.g., 2025)\n");
            eprintln!("ENVIRONMENT VARIABLES:");
            eprintln!("    PAPERLESS_URL       Paperless-ngx base URL (e.g., https://paperless.example.com)");
            eprintln!("    PAPERLESS_API_KEY   API token for authentication\n");
            eprintln!("EXAMPLE:");
            eprintln!("    export PAPERLESS_URL=https://paperless.example.com");
            eprintln!("    export PAPERLESS_API_KEY=your_api_token");
            eprintln!("    paperless-to-spreadsheet 2025");
            std::process::exit(1);
        }
    };

    // Get environment variables
    let paperless_url = env::var("PAPERLESS_URL")
        .context("PAPERLESS_URL environment variable not set")?;
    let api_key = env::var("PAPERLESS_API_KEY")
        .context("PAPERLESS_API_KEY environment variable not set")?;

    let client = Client::new();
    let base_url = paperless_url.trim_end_matches('/');

    // Step 1: Get custom field information to find the financial year field
    let custom_fields = get_custom_fields(&client, base_url, &api_key).await?;
    let financial_year_field = custom_fields.iter()
        .find(|field| field.id == 1)
        .context("Custom field with ID 1 (financial year) not found")?;

    println!("Found financial year field: {} (ID: {})", financial_year_field.name, financial_year_field.id);

    // Step 2: Get all tags to find the "Tax", "multi-fy", and "IP" tags
    let tags = get_tags(&client, base_url, &api_key).await?;

    let tax_tag_id = tags.iter()
        .find(|tag| tag.name == "Tax")
        .map(|tag| tag.id)
        .context("Tax tag not found. All documents should be tagged with 'Tax'")?;

    println!("Found Tax tag with ID: {}", tax_tag_id);

    let multi_fy_tag_id = tags.iter()
        .find(|tag| tag.name == "multi-fy")
        .map(|tag| tag.id);

    if let Some(multi_fy_id) = multi_fy_tag_id {
        println!("Found multi-fy tag with ID: {}", multi_fy_id);
    } else {
        println!("Warning: No 'multi-fy' tag found.");
    }

    let ip_tag_id = tags.iter()
        .find(|tag| tag.name == "IP")
        .map(|tag| tag.id);

    if let Some(ip_id) = ip_tag_id {
        println!("Found IP tag with ID: {}", ip_id);
    } else {
        println!("Warning: No 'IP' tag found. All documents will be classified as Work Expenses.");
    }

    // Step 3: Get correspondents and document types for name lookup
    let correspondents = get_correspondents(&client, base_url, &api_key).await?;
    let document_types = get_document_types(&client, base_url, &api_key).await?;

    println!("Loaded {} correspondents and {} document types", correspondents.len(), document_types.len());

    // Step 4: Get all documents tagged with "Tax" and filter them
    let tax_documents = get_documents_by_tax_tag(&client, base_url, &api_key, tax_tag_id).await?;
    println!("Found {} documents tagged with Tax", tax_documents.len());

    // Step 5: Filter documents to only include those that match our criteria
    let filtered_documents = filter_documents_by_criteria(tax_documents, &fy, multi_fy_tag_id);
    println!("Filtered to {} documents for FY {}", filtered_documents.len(), fy);

    // Step 6: Separate documents into work expenses and investment property
    let (mut work_expenses, mut investment_property) = separate_documents_by_tag(filtered_documents, ip_tag_id, &correspondents, &document_types, base_url, &client, &api_key).await?;

    // Step 7: Sort documents with Statements first
    sort_documents_by_type(&mut work_expenses);
    sort_documents_by_type(&mut investment_property);

    println!("Work Expenses: {} documents", work_expenses.len());
    println!("Investment Property: {} documents", investment_property.len());

    // Step 8: Create Excel spreadsheet with two worksheets
    let filename = format!("FY{} Documents.xlsx", fy);
    create_excel_file_with_worksheets(&work_expenses, &investment_property, &filename)?;

    println!("Successfully created {}", filename);

    Ok(())
}

async fn get_custom_fields(client: &Client, base_url: &str, api_key: &str) -> Result<Vec<CustomField>> {
    let url = format!("{}/api/custom_fields/", base_url);

    let response = client
        .get(&url)
        .header("Authorization", format!("Token {}", api_key))
        .send()
        .await
        .context("Failed to fetch custom fields")?;

    if !response.status().is_success() {
        return Err(anyhow!("Failed to fetch custom fields: {}", response.status()));
    }

    let custom_fields_response: CustomFieldsResponse = response
        .json()
        .await
        .context("Failed to parse custom fields response")?;

    Ok(custom_fields_response.results)
}

async fn get_tags(client: &Client, base_url: &str, api_key: &str) -> Result<Vec<Tag>> {
    let url = format!("{}/api/tags/", base_url);

    let response = client
        .get(&url)
        .header("Authorization", format!("Token {}", api_key))
        .send()
        .await
        .context("Failed to fetch tags")?;

    if !response.status().is_success() {
        return Err(anyhow!("Failed to fetch tags: {}", response.status()));
    }

    let tags_response: TagsResponse = response
        .json()
        .await
        .context("Failed to parse tags response")?;

    Ok(tags_response.results)
}

async fn get_correspondents(client: &Client, base_url: &str, api_key: &str) -> Result<Vec<Correspondent>> {
    let mut all_correspondents = Vec::new();
    let mut next_url = Some(format!("{}/api/correspondents/", base_url));

    while let Some(url) = next_url {
        let response = client
            .get(&url)
            .header("Authorization", format!("Token {}", api_key))
            .send()
            .await
            .context("Failed to fetch correspondents")?;

        if !response.status().is_success() {
            return Err(anyhow!("Failed to fetch correspondents: {}", response.status()));
        }

        let correspondents_response: CorrespondentsResponse = response
            .json()
            .await
            .context("Failed to parse correspondents response")?;

        all_correspondents.extend(correspondents_response.results);
        next_url = correspondents_response.next;
    }

    Ok(all_correspondents)
}

async fn get_document_types(client: &Client, base_url: &str, api_key: &str) -> Result<Vec<DocumentType>> {
    let mut all_document_types = Vec::new();
    let mut next_url = Some(format!("{}/api/document_types/", base_url));

    while let Some(url) = next_url {
        let response = client
            .get(&url)
            .header("Authorization", format!("Token {}", api_key))
            .send()
            .await
            .context("Failed to fetch document types")?;

        if !response.status().is_success() {
            return Err(anyhow!("Failed to fetch document types: {}", response.status()));
        }

        let document_types_response: DocumentTypesResponse = response
            .json()
            .await
            .context("Failed to parse document types response")?;

        all_document_types.extend(document_types_response.results);
        next_url = document_types_response.next;
    }

    Ok(all_document_types)
}

async fn get_documents_by_tax_tag(
    client: &Client,
    base_url: &str,
    api_key: &str,
    tax_tag_id: u32
) -> Result<Vec<Document>> {
    let mut all_documents = Vec::new();
    let mut next_url = Some(format!(
        "{}/api/documents/?tags__id__in={}",
        base_url,
        tax_tag_id
    ));

    while let Some(url) = next_url {
        let response = client
            .get(&url)
            .header("Authorization", format!("Token {}", api_key))
            .send()
            .await
            .context("Failed to fetch documents")?;

        if !response.status().is_success() {
            return Err(anyhow!("Failed to fetch documents: {}", response.status()));
        }

        let documents_response: DocumentsResponse = response
            .json()
            .await
            .context("Failed to parse documents response")?;

        all_documents.extend(documents_response.results);
        next_url = documents_response.next;
    }

    Ok(all_documents)
}

fn filter_documents_by_criteria(
    documents: Vec<Document>,
    target_fy: &str,
    multi_fy_tag_id: Option<u32>
) -> Vec<Document> {
    documents.into_iter().filter(|doc| {
        // Check if document has multi-fy tag
        if let Some(multi_fy_id) = multi_fy_tag_id {
            if doc.tags.contains(&multi_fy_id) {
                return true; // Document has multi-fy tag, include it
            }
        }

        // Check if document has the target financial year in custom field ID 1
        for cf in &doc.custom_fields {
            if cf.field == 1 { // Financial year field
                let value_str = match &cf.value {
                    Value::String(s) => s.clone(),
                    Value::Number(n) => n.to_string(),
                    _ => cf.value.to_string().trim_matches('"').to_string(),
                };
                if value_str == target_fy {
                    return true; // Document has matching financial year
                }
            }
        }

        false // Document doesn't match either criteria
    }).collect()
}

async fn separate_documents_by_tag(
    documents: Vec<Document>,
    ip_tag_id: Option<u32>,
    correspondents: &[Correspondent],
    document_types: &[DocumentType],
    base_url: &str,
    client: &Client,
    api_key: &str,
) -> Result<(Vec<InvoiceData>, Vec<InvoiceData>)> {
    let mut work_expenses = Vec::new();
    let mut investment_property = Vec::new();

    for doc in documents {
        let invoice_data = process_single_document(doc, correspondents, document_types, base_url, client, api_key).await?;

        // Check if document has IP tag
        if let Some(ip_id) = ip_tag_id {
            if invoice_data.0.tags.contains(&ip_id) {
                investment_property.push(invoice_data.1);
            } else {
                work_expenses.push(invoice_data.1);
            }
        } else {
            // No IP tag found, put everything in work expenses
            work_expenses.push(invoice_data.1);
        }
    }

    Ok((work_expenses, investment_property))
}

async fn process_single_document(
    doc: Document,
    correspondents: &[Correspondent],
    document_types: &[DocumentType],
    base_url: &str,
    client: &Client,
    api_key: &str,
) -> Result<(Document, InvoiceData)> {
    // Extract custom field values
    let mut custom_values: HashMap<u32, String> = HashMap::new();
    for cf in &doc.custom_fields {
        let value_str = match &cf.value {
            Value::String(s) => s.clone(),
            Value::Number(n) => n.to_string(),
            Value::Bool(b) => b.to_string(),
            Value::Null => String::new(),
            _ => cf.value.to_string().trim_matches('"').to_string(),
        };
        custom_values.insert(cf.field, value_str);
    }

    let title = doc.title.clone();
    let date = doc.created_date.clone();

    // Get correspondent name
    let counterparty = if let Some(correspondent_id) = doc.correspondent {
        correspondents.iter()
            .find(|c| c.id == correspondent_id)
            .map(|c| c.name.clone())
            .unwrap_or_else(|| format!("Unknown (ID: {})", correspondent_id))
    } else {
        String::new()
    };

    // Get document type name
    let doc_type = if let Some(type_id) = doc.document_type {
        document_types.iter()
            .find(|dt| dt.id == type_id)
            .map(|dt| dt.name.clone())
            .unwrap_or_else(|| format!("Unknown (ID: {})", type_id))
    } else {
        String::new()
    };

    // Handle amount and currency based on document type
    let (currency, amount) = if doc_type.to_lowercase().contains("statement") {
        // Statements should have blank amounts
        (String::new(), String::new())
    } else {
        // Get amount from custom field ID 3 (amount field) and parse currency
        let raw_amount = custom_values.get(&3)
            .map(|v| if v.is_empty() { "0" } else { v })
            .unwrap_or("0");

        parse_amount_and_currency(raw_amount)
    };

    // Create share link
    println!("Creating share link for document: {}", doc.title);
    let share_link = match create_share_link(client, base_url, api_key, doc.id).await {
        Ok(link) => link,
        Err(e) => {
            eprintln!("Warning: Failed to create share link for document {}: {}", doc.id, e);
            String::new() // Use empty string if share link creation fails
        }
    };

    // Create link to paperless document
    let link = format!("{}/documents/{}", base_url, doc.id);

    // Notes column is empty
    let notes = String::new();

    let invoice_data = InvoiceData {
        title,
        counterparty,
        date,
        doc_type,
        amount,
        currency,
        notes,
        share_link,
        link,
    };

    Ok((doc, invoice_data))
}

fn sort_documents_by_type(documents: &mut Vec<InvoiceData>) {
    documents.sort_by(|a, b| {
        let a_is_statement = a.doc_type.to_lowercase().contains("statement");
        let b_is_statement = b.doc_type.to_lowercase().contains("statement");

        match (a_is_statement, b_is_statement) {
            (true, false) => std::cmp::Ordering::Less,    // Statements first
            (false, true) => std::cmp::Ordering::Greater, // Non-statements after
            _ => std::cmp::Ordering::Equal,               // Keep existing order within same type
        }
    });
}

fn create_worksheet(
    workbook: &mut Workbook,
    data: &[InvoiceData],
    worksheet_name: &str
) -> Result<()> {
    let worksheet = workbook.add_worksheet().set_name(worksheet_name)?;

    // Create header format
    let header_format = Format::new()
        .set_bold()
        .set_background_color(Color::RGB(0xD3D3D3));

    // Write headers - Title, Counterparty, Date, Type, Amount, Currency, Notes, Share Link, Link
    let headers = ["Title", "Counterparty", "Date", "Type", "Amount", "Currency", "Notes", "Share Link", "Link"];
    for (col, &header) in headers.iter().enumerate() {
        worksheet.write_string_with_format(0, col as u16, header, &header_format)?;
    }

    // Write data
    for (row, invoice) in data.iter().enumerate() {
        let row_num = (row + 1) as u32;
        worksheet.write_string(row_num, 0, &invoice.title)?;
        worksheet.write_string(row_num, 1, &invoice.counterparty)?;
        worksheet.write_string(row_num, 2, &invoice.date)?;
        worksheet.write_string(row_num, 3, &invoice.doc_type)?;
        worksheet.write_string(row_num, 4, &invoice.amount)?;
        worksheet.write_string(row_num, 5, &invoice.currency)?;
        worksheet.write_string(row_num, 6, &invoice.notes)?;
        if !invoice.share_link.is_empty() {
            worksheet.write_url_with_text(row_num, 7, Url::new(&invoice.share_link), "Share Link")?;
        }
        worksheet.write_url_with_text(row_num, 8, Url::new(&invoice.link), "Link")?;
    }

    // Auto-fit columns
    worksheet.autofit();

    Ok(())
}

fn create_excel_file_with_worksheets(
    work_expenses: &[InvoiceData],
    investment_property: &[InvoiceData],
    filename: &str
) -> Result<()> {
    let mut workbook = Workbook::new();

    // Create Work Expenses worksheet
    create_worksheet(&mut workbook, work_expenses, "Work Expenses")?;

    // Create Investment Property worksheet (only if there are IP documents)
    if !investment_property.is_empty() {
        create_worksheet(&mut workbook, investment_property, "Investment Property")?;
    }

    workbook.save(filename)?;
    Ok(())
}
