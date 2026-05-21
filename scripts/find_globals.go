package main

import (
	"fmt"
	"os"
	"github.com/sleepy/sleepy.go"
)

func main() {
	content, _ := os.ReadFile("../sleepy.go/examples/SA.cna")
	parser := sleepy.NewParser()
	defer parser.Close()
	node, _ := parser.Parse(string(content))

	globals := make(map[string]bool)
	for _, child := range node.Children {
		if child.Type == sleepy.AstAssignment {
			target := child.Children[0]
			if target.Type == sleepy.AstScalar || target.Type == sleepy.AstHashtable || target.Type == sleepy.AstArray {
				if name, ok := target.Value.(string); ok {
					globals[name] = true
				}
			}
		}
	}
	fmt.Println("Top-level globals:", globals)
}
