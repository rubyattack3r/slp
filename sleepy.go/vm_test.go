package sleepy

import (
	"testing"
)

func TestVMInterpret(t *testing.T) {
	vm := NewVM()
	defer vm.Close()

	source := "assert(1 + 2 == 3);"

	err := vm.Interpret(source)
	if err != nil {
		t.Fatalf("Expected VM to execute successfully, got error: %v", err)
	}
}
