package sleepy

import "testing"

func TestParserSuccess(t *testing.T) {
	parser := NewParser()
	source := `println("Hello World");`
	
	success, err := parser.Parse(source)
	if err != nil {
		t.Fatalf("Expected parsing to succeed, got error: %v", err)
	}
	if success == nil {
		t.Fatalf("Expected success to not be nil")
	}
}

func TestParserFailure(t *testing.T) {
	parser := NewParser()
	source := `println("Hello World"` // Missing closing paren
	
	success, _ := parser.Parse(source)
	if success != nil {
		t.Fatalf("Expected parsing to fail")
	}
}
