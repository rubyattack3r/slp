package main

import (
	"fmt"
	"os"

	"github.com/sleepy/sleepy.go"
)

var functionName string
var newName string

// replaceFunctionCalls performs an in-place AST modification.
// It looks for AstCall nodes where the target is an identifier matching `functionName`
// and swaps the string value of the AST node to `newName`.
func replaceFunctionCalls(node *sleepy.Node) {
	if node == nil {
		return
	}

	// In Sleepy, function calls have a child defining the function name (usually AstString or AstIdentifier)
	if node.Type == sleepy.AstCall {
		if str, ok := node.Value.(string); ok && str == functionName {
			// We match the function call name, so we overwrite the value within the AST
			node.Value = newName
		}
	}

	// Recursively walk and replace
	for _, child := range node.Children {
		replaceFunctionCalls(child)
	}
}

func main() {
	if len(os.Args) < 4 {
		fmt.Printf("Usage: %s <function name> <new name> <script path>\n", os.Args[0])
		os.Exit(1)
	}

	functionName = os.Args[1]
	newName = os.Args[2]
	path := os.Args[3]

	content, err := os.ReadFile(path)
	if err != nil {
		fmt.Printf("Error reading file: %v\n", err)
		os.Exit(1)
	}

	parser := sleepy.NewParser()
	defer parser.Free()

	// Parse Sleepy Content
	scriptNode, err := parser.Parse(string(content))
	if err != nil {
		// Print parsing errors if they occurred
		fmt.Printf("Parse Error: %v\n", err)
	}

	if scriptNode != nil {
		// Walk the AST and modify it in-place
		replaceFunctionCalls(scriptNode)

		// Print out the modified AST
		// Since the Go bindings do not yet have an `ast.format` roundtrip
		// we will showcase that the AST has been successfully modified by printing it.
		// (Implementation of an AST-to-text stringifier unparser would go here)

		fmt.Printf("; AST Function replacement applied: %s -> %s\n", functionName, newName)
		fmt.Println("; (Note: Serializing the AST back to string currently requires formatting logic)")
		
		var walkPrint func(*sleepy.Node, int)
		walkPrint = func(n *sleepy.Node, depth int) {
			if n == nil {
				return
			}
			indent := ""
			for i := 0; i < depth; i++ {
				indent += "  "
			}
			fmt.Printf("%sNode Type: %d", indent, n.Type)
			if n.Value != nil {
				fmt.Printf(" Value: %v", n.Value)
			}
			fmt.Println()
			for _, c := range n.Children {
				walkPrint(c, depth+1)
			}
		}
		walkPrint(scriptNode, 0)
	}
}
