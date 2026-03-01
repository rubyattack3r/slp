//go:build ignore

package main

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/sleepy/sleepy.go"
)

func main() {
	jsonData := `{"foo": "bar", "num": 123, "active": true}`
	var data map[string]interface{}
	if err := json.Unmarshal([]byte(jsonData), &data); err != nil {
		fmt.Printf("JSON Error: %v\n", err)
		os.Exit(1)
	}

	parser := sleepy.NewParser()
	defer parser.Close()

	// Goal: Create blah = { keys... }
	// We'll build this as an assignment of a block to a scalar
	
	block := &sleepy.Node{
		Type: sleepy.AstBlock,
	}

	for k, v := range data {
		// Represent each key-value pair as an assignment inside the block
		// e.g. $k = "v";
		assign := &sleepy.Node{
			Type: sleepy.AstAssignment,
			Children: []*sleepy.Node{
				{
					Type:  sleepy.AstScalar,
					Value: k,
				},
				convertToNode(v),
			},
		}
		block.Children = append(block.Children, assign)
	}

	root := &sleepy.Node{
		Type: sleepy.AstAssignment,
		Children: []*sleepy.Node{
			{
				Type:  sleepy.AstIdentifier,
				Value: "blah",
			},
			block,
		},
	}

	fmt.Println("Resulting Sleepy Code:")
	fmt.Println(root.Format(parser))
}

func convertToNode(v interface{}) *sleepy.Node {
	switch val := v.(type) {
	case string:
		return &sleepy.Node{Type: sleepy.AstString, Value: fmt.Sprintf("\"%s\"", val)}
	case float64:
		return &sleepy.Node{Type: sleepy.AstNumber, Value: val}
	case bool:
		return &sleepy.Node{Type: sleepy.AstBoolean, Value: val}
	default:
		return &sleepy.Node{Type: sleepy.AstString, Value: "\"unknown\""}
	}
}
