package main

import (
	"fmt"
	"os"

	"github.com/sleepy/sleepy.go"
)

func main() {
	if len(os.Args) < 4 {
		fmt.Printf("Usage: %s <old_function> <new_function> <script_path>\n", os.Args[0])
		os.Exit(1)
	}

	oldFunc := os.Args[1]
	newFunc := os.Args[2]
	scriptPath := os.Args[3]

	content, err := os.ReadFile(scriptPath)
	if err != nil {
		fmt.Printf("Error reading file: %v\n", err)
		os.Exit(1)
	}

	parser := sleepy.NewParser()
	defer parser.Free()

	// Parse Sleepy Content
	root, err := parser.Parse(string(content))
	if err != nil {
		fmt.Printf("Parse Error: %v\n", err)
		os.Exit(1)
	}

	// Replacement logic
	replaceFn := func(node *sleepy.Node) *sleepy.Node {
		if node.Type == sleepy.AstCall && node.Value == oldFunc {
			node.Value = newFunc
		}
		return node
	}

	// Transform the AST
	root = root.Walk(replaceFn)

	// Format back to Sleepy code
	fmt.Println(root.Format())
}
