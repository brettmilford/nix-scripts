package transaction

import (
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestTransactionList_AddTransaction(t *testing.T) {
	tl := &TransactionList{}
	
	tx := Transaction{
		ID:          "test-1",
		Date:        time.Now(),
		Description: "Test transaction",
		Amount:      -50.00,
		Category:    "Food & dining",
		Source:      "CBA",
	}
	
	tl.AddTransaction(tx)
	
	assert.Equal(t, 1, tl.Total)
	assert.Equal(t, 1, len(tl.Transactions))
	assert.Equal(t, "test-1", tl.Transactions[0].ID)
}

func TestTransactionList_GetByCategory(t *testing.T) {
	tl := &TransactionList{}
	
	tx1 := Transaction{
		ID:       "tx-1",
		Category: "Food & dining",
		Amount:   -25.50,
	}
	tx2 := Transaction{
		ID:       "tx-2", 
		Category: "Groceries & household",
		Amount:   -75.00,
	}
	tx3 := Transaction{
		ID:       "tx-3",
		Category: "Food & dining", 
		Amount:   -15.25,
	}
	
	tl.AddTransaction(tx1)
	tl.AddTransaction(tx2)
	tl.AddTransaction(tx3)
	
	foodTransactions := tl.GetByCategory("Food & dining")
	assert.Equal(t, 2, len(foodTransactions))
	assert.Equal(t, "tx-1", foodTransactions[0].ID)
	assert.Equal(t, "tx-3", foodTransactions[1].ID)
	
	groceryTransactions := tl.GetByCategory("Groceries & household")
	assert.Equal(t, 1, len(groceryTransactions))
	assert.Equal(t, "tx-2", groceryTransactions[0].ID)
}