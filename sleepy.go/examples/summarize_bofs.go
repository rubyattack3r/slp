//go:build ignore

package main

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/sleepy/sleepy.go"
)

// Helper type to mirror the Python JsonEncoder
type BofMap map[string]BofMetadata

type BofMetadata struct {
	HelpShort string `json:"help_short"`
	HelpLong  string `json:"help_long"`
}

var beaconCommands = make(BofMap)

func concatenateStrings(node *sleepy.Node) string {
	if node == nil {
		return ""
	}
	
	// If the node is an argument, unbox its child
	if node.Type == sleepy.AstArg && len(node.Children) > 0 {
		return concatenateStrings(node.Children[0])
	}

	if node.Type == sleepy.AstLiteral || node.Type == sleepy.AstString { 
		if str, ok := node.Value.(string); ok {
			// Strip quotes since C AST preserves them
			if len(str) >= 2 && (str[0] == '"' || str[0] == '\'') {
				return str[1:len(str)-1]
			}
			return str
		}
	} else if node.Type == sleepy.AstBinop {
		// Assume concatenation for simplicity
		if len(node.Children) == 2 {
			return concatenateStrings(node.Children[0]) + concatenateStrings(node.Children[1])
		}
	}
	return ""
}

func walk(node *sleepy.Node, callback func(*sleepy.Node)) {
	if node == nil {
		return
	}
	callback(node)
	for _, child := range node.Children {
		walk(child, callback)
	}
}

func findBeaconCommandRegister(node *sleepy.Node) {
	// SLEEPY_AST_CALL = 15
	if node.Type == sleepy.AstCall && node.Value == "beacon_command_register" && len(node.Children) >= 3 {
		// Assuming args are at indices 0, 1, 2
		arg0 := node.Children[0]
		arg1 := node.Children[1]
		arg2 := node.Children[2]

		name := concatenateStrings(arg0)
		if name != "" {
			if _, exists := beaconCommands[name]; !exists {
				beaconCommands[name] = BofMetadata{}
			}
			meta := beaconCommands[name]
			meta.HelpShort = concatenateStrings(arg1)
			meta.HelpLong = concatenateStrings(arg2)
			beaconCommands[name] = meta
		}
	}
}

// No sub path searching required as we only extract basic metadata now

func main() {
	if len(os.Args) < 2 {
		fmt.Printf("Usage: %s <path>\n", os.Args[0])
		os.Exit(1)
	}

	path := os.Args[1]
	content, err := os.ReadFile(path)
	if err != nil {
		fmt.Printf("Error reading file: %v\n", err)
		os.Exit(1)
	}

	parser := sleepy.NewParser()
	defer parser.Close()

	// Parse Sleepy Content
	scriptNode, err := parser.Parse(string(content))
	if err != nil {
		fmt.Printf("Parse Error: %v\n", err)
	}

	// Extract basic information
	walk(scriptNode, findBeaconCommandRegister)

	// Output as JSON
	bytes, err := json.MarshalIndent(beaconCommands, "", "    ")
	if err != nil {
		fmt.Printf("JSON encoding error: %v\n", err)
		return
	}

	fmt.Println(string(bytes))
}

