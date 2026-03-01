package sleepy

import (
	"strings"
	"testing"
)

func TestParserSuccess(t *testing.T) {
	parser := NewParser()
	defer parser.Close()
	source := `println("Hello World");`
	
	node, err := parser.Parse(source)
	if err != nil {
		t.Fatalf("Expected parsing to succeed, got error: %v", err)
	}
	if node == nil {
		t.Fatalf("Expected node to not be nil")
	}
}

func TestParserFailure(t *testing.T) {
	parser := NewParser()
	defer parser.Close()
	source := `println("Hello World"` // Missing closing paren
	
	node, err := parser.Parse(source)
	if err == nil || node != nil {
		t.Fatalf("Expected parsing to fail")
	}
}

func TestASTFormat(t *testing.T) {
	parser := NewParser()
	defer parser.Close()
	source := `println("Hello", 123, $true);`
	node, err := parser.Parse(source)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	formatted := node.Format()
	expected := `println("Hello", 123, $true)`
	if !strings.Contains(formatted, expected) {
		t.Errorf("Format() output did not contain expected code.\nGot: %s\nExpected: %s", formatted, expected)
	}
}

func TestASTWalk(t *testing.T) {
	parser := NewParser()
	defer parser.Close()
	source := `foo(); bar();`
	node, err := parser.Parse(source)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	callCount := 0
	node.Walk(func(n *Node) *Node {
		if n != nil && n.Type == AstCall {
			callCount++
			if n.Value == "foo" {
				n.Value = "baz"
			}
		}
		return n
	})

	if callCount != 2 {
		t.Errorf("Expected to walk 2 function calls, found %d", callCount)
	}

	formatted := node.Format()
	if !strings.Contains(formatted, "baz()") || !strings.Contains(formatted, "bar()") {
		t.Errorf("Walk modification failed, got: %s", formatted)
	}
}

func TestASTWalkIf(t *testing.T) {
	parser := NewParser()
	defer parser.Close()
	source := `if ($x > 5) { foo(); } else { bar(); }`
	node, err := parser.Parse(source)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	callCount := 0
	node.Walk(func(n *Node) *Node {
		if n != nil && n.Type == AstCall {
			callCount++
			if n.Value == "foo" {
				n.Value = "baz"
			}
		}
		return n
	})

	if callCount != 2 {
		t.Errorf("Expected to walk 2 function calls inside if/else branches, found %d", callCount)
	}

	formatted := node.Format()
	if !strings.Contains(formatted, "baz()") {
		t.Errorf("Walk modification inside then_branch failed, got: %s", formatted)
	}
}
