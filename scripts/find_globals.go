package main

import (
	"fmt"
	"os"
	"github.com/slp-lang/slp.go"
)

func main() {
	content, _ := os.ReadFile("../slp.go/examples/SA.cna")
	parser := slp.NewParser()
	defer parser.Close()
	node, _ := parser.Parse(string(content))

	globals := make(map[string]bool)
	for _, child := range node.Children {
		if child.Type == slp.AstAssignment {
			target := child.Children[0]
			if target.Type == slp.AstScalar || target.Type == slp.AstHashtable || target.Type == slp.AstArray {
				if name, ok := target.Value.(string); ok {
					globals[name] = true
				}
			}
		}
	}
	fmt.Println("Top-level globals:", globals)
}
