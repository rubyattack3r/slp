package main

import (
	"fmt"
	"os"
	"github.com/sleepy/sleepy.go"
)

func printAST(node *sleepy.Node, indent string) {
	if node == nil { return }
	fmt.Printf("%s%s (%v)\n", indent, node.Type.String(), node.Value)
	for _, child := range node.Children {
		printAST(child, indent+"  ")
	}
}

func main() {
	content, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Println("Err:", err)
		return
	}
	parser := sleepy.NewParser()
	defer parser.Close()
	node, err := parser.Parse(string(content))
	if err != nil {
		fmt.Println("Parse err:", err)
		return
	}
	printAST(node, "")
}
