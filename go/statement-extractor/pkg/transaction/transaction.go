package transaction

import (
	"time"
)

// Transaction represents a single financial transaction
type Transaction struct {
	ID          string    `json:"id"`
	Date        time.Time `json:"date"`
	Description string    `json:"description"`
	Amount      float64   `json:"amount"`
	Balance     float64   `json:"balance,omitempty"`
	Category    string    `json:"category"`
	Source      string    `json:"source"` // e.g., "CBA", "ANZ"
}

// TransactionList holds a collection of transactions
type TransactionList struct {
	Transactions []Transaction `json:"transactions"`
	Total        int           `json:"total"`
	Source       string        `json:"source"`
	ProcessedAt  time.Time     `json:"processed_at"`
}

// AddTransaction appends a transaction to the list
func (tl *TransactionList) AddTransaction(t Transaction) {
	tl.Transactions = append(tl.Transactions, t)
	tl.Total = len(tl.Transactions)
}

// GetByCategory returns all transactions matching the given category
func (tl *TransactionList) GetByCategory(category string) []Transaction {
	var filtered []Transaction
	for _, t := range tl.Transactions {
		if t.Category == category {
			filtered = append(filtered, t)
		}
	}
	return filtered
}