package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/sleepy/sleepy.go"
)

var globalAliases = make(map[string]string)
var globalCommands = make(map[string]string)

// parseAndProcessFile handles parsing, script_resource rewriting, and AST inlining for include()
func parseAndProcessFile(filePath string, parser *sleepy.Parser, visited map[string]bool) (*sleepy.Node, error) {
	absPath, err := filepath.Abs(filePath)
	if err != nil {
		return nil, err
	}
	if visited[absPath] {
		return &sleepy.Node{Type: sleepy.AstBlock}, nil // prevent infinite recursion
	}
	visited[absPath] = true

	content, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}

	node, err := parser.Parse(string(content))
	if err != nil {
		return nil, fmt.Errorf("in %s: %v", filePath, err)
	}

	scriptDir := filepath.Dir(filePath)

	// 1. Rewrite script_resource immediately for this file
	node = node.Walk(func(n *sleepy.Node) *sleepy.Node {
		if n == nil {
			return nil
		}
		if n.Type == sleepy.AstCall {
			if name, ok := n.Value.(string); ok && name == "script_resource" && scriptDir != "." && scriptDir != "" {
				if len(n.Children) > 0 {
					origArg := n.Children[0]
					newArg := &sleepy.Node{
						Type:  sleepy.AstBinop,
						Value: ".",
						Children: []*sleepy.Node{
							{
								Type:  sleepy.AstString,
								Value: fmt.Sprintf("\"%s/\"", filepath.ToSlash(scriptDir)),
							},
							origArg,
						},
					}
					n.Children[0] = newArg
				}
			}
		}
		return n
	})

	// 2. AST Inlining for includes
	var inlineWalk func(n *sleepy.Node) *sleepy.Node
	inlineWalk = func(n *sleepy.Node) *sleepy.Node {
		if n == nil {
			return nil
		}

		if n.Type == sleepy.AstCall && n.Value == "include" && len(n.Children) == 1 {
			argNode := n.Children[0]
			if argNode.Type == sleepy.AstArg && len(argNode.Children) > 0 {
				strNode := argNode.Children[0]
				if strNode.Type == sleepy.AstString || strNode.Type == sleepy.AstLiteral {
					if relPath, ok := strNode.Value.(string); ok {
						relPath = strings.Trim(relPath, "\"'`")
						incPath := filepath.Join(scriptDir, relPath)
						includedNode, err := parseAndProcessFile(incPath, parser, visited)
						if err != nil {
							fmt.Printf("WARNING: Failed to inline %s: %v\n", incPath, err)
						} else if includedNode != nil {
							return &sleepy.Node{
								Type:     sleepy.AstBlock,
								Children: includedNode.Children,
							}
						}
					}
				}
			}
		}

		for i, c := range n.Children {
			n.Children[i] = inlineWalk(c)
		}
		return n
	}

	node = inlineWalk(node)
	return node, nil
}

// discoverGlobals recursively tracks local() variables to identify deep implicit globals
func discoverGlobals(node *sleepy.Node, localVars map[string]bool, globals map[string]bool) {
	if node == nil {
		return
	}

	currentLocals := localVars
	if node.Type == sleepy.AstEnvBridge && (node.Value == "sub" || node.Value == "alias" || node.Value == "lambda") {
		currentLocals = make(map[string]bool)
		for k, v := range localVars {
			currentLocals[k] = v
		}
	}

	if node.Type == sleepy.AstCall && node.Value == "local" && len(node.Children) > 0 {
		argNode := node.Children[0]
		if argNode.Type == sleepy.AstArg && len(argNode.Children) > 0 {
			strNode := argNode.Children[0]
			if strNode.Type == sleepy.AstString || strNode.Type == sleepy.AstLiteral {
				if val, ok := strNode.Value.(string); ok {
					vars := strings.Split(val, " ")
					for _, v := range vars {
						v = strings.TrimSpace(v)
						if len(v) > 1 && (v[0] == '$' || v[0] == '%' || v[0] == '@') {
							currentLocals[v[1:]] = true
						}
					}
				}
			}
		}
	}

	if node.Type == sleepy.AstAssignment {
		target := node.Children[0]
		if target.Type == sleepy.AstScalar || target.Type == sleepy.AstHashtable || target.Type == sleepy.AstArray {
			if name, ok := target.Value.(string); ok {
				isDigit := true
				for _, c := range name {
					if c < '0' || c > '9' {
						isDigit = false
						break
					}
				}
				if !isDigit && !currentLocals[name] && name != "null" && name != "true" && name != "false" {
					globals[name] = true
				}
			}
		}
	}

	for _, child := range node.Children {
		discoverGlobals(child, currentLocals, globals)
	}
}

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <output.cna> <input1.cna> [input2.cna...]\n", os.Args[0])
		os.Exit(1)
	}

	outputFile := os.Args[1]
	inputFiles := os.Args[2:]

	parser := sleepy.NewParser()
	defer parser.Close()

	root := &sleepy.Node{Type: sleepy.AstScript}

	for _, inputFile := range inputFiles {
		visited := make(map[string]bool)
		node, err := parseAndProcessFile(inputFile, parser, visited)
		if err != nil {
			fmt.Printf("Error processing %s: %v\n", inputFile, err)
			os.Exit(1)
		}

		projectName := filepath.Base(filepath.Dir(inputFile))
		if projectName == "." || projectName == "" {
			projectName = "bundle"
		}
		prefix := strings.ReplaceAll(projectName, "-", "_") + "_"

		localSubs := make(map[string]bool)
		localGlobals := make(map[string]bool)

		// Pass 1: Scope Analysis for Globals
		discoverGlobals(node, make(map[string]bool), localGlobals)

		// Pass 1.5: Discover Subs and Public Collisions
		node.Walk(func(n *sleepy.Node) *sleepy.Node {
			if n == nil {
				return nil
			}
			if n.Type == sleepy.AstEnvBridge && n.Value == "sub" {
				for _, c := range n.Children {
					if c.Type == sleepy.AstIdentifier {
						if name, ok := c.Value.(string); ok {
							localSubs[name] = true
						}
					}
				}
			} else if n.Type == sleepy.AstEnvBridge && n.Value == "alias" {
				for _, c := range n.Children {
					if c.Type == sleepy.AstIdentifier {
						if name, ok := c.Value.(string); ok {
							if owner, exists := globalAliases[name]; exists {
								fmt.Printf("\n[!] WARNING: Public alias collision detected for '%s'\n", name)
								fmt.Printf("    -> Registered in both '%s' and '%s'\n", owner, projectName)
							}
							globalAliases[name] = projectName
						}
					}
				}
			} else if n.Type == sleepy.AstCall && n.Value == "beacon_command_register" && len(n.Children) > 0 {
				argNode := n.Children[0]
				if argNode.Type == sleepy.AstArg && len(argNode.Children) > 0 {
					strNode := argNode.Children[0]
					if strNode.Type == sleepy.AstString || strNode.Type == sleepy.AstLiteral {
						if name, ok := strNode.Value.(string); ok {
							if owner, exists := globalCommands[name]; exists {
								fmt.Printf("\n[!] WARNING: Command registry collision detected for '%s'\n", name)
								fmt.Printf("    -> Registered in both '%s' and '%s'\n", owner, projectName)
							}
							globalCommands[name] = projectName
						}
					}
				}
			}
			return n
		})

		// Pass 2: Rewrite Subroutines, Variables, and Function References
		node = node.Walk(func(n *sleepy.Node) *sleepy.Node {
			if n == nil {
				return nil
			}

			// Rename sub declarations
			if n.Type == sleepy.AstEnvBridge {
				if kw, ok := n.Value.(string); ok && kw == "sub" {
					for _, child := range n.Children {
						if child.Type == sleepy.AstIdentifier {
							if name, ok := child.Value.(string); ok && localSubs[name] {
								child.Value = prefix + name
							}
						}
					}
				}
			}

			// Rename variables
			if n.Type == sleepy.AstScalar || n.Type == sleepy.AstHashtable || n.Type == sleepy.AstArray {
				if name, ok := n.Value.(string); ok && localGlobals[name] {
					n.Value = prefix + name
				}
			}

			// Rename function references (&func)
			if n.Type == sleepy.AstAddress {
				if name, ok := n.Value.(string); ok && localSubs[name] {
					n.Value = prefix + name
				}
			}

			// Rename calls to local subs and local() variable references
			if n.Type == sleepy.AstCall {
				if name, ok := n.Value.(string); ok {
					if localSubs[name] {
						n.Value = prefix + name
					} else if name == "local" {
						// string replacements inside local() strings
						if len(n.Children) > 0 && n.Children[0].Type == sleepy.AstArg {
							argStrNode := n.Children[0].Children[0]
							if argStrNode.Type == sleepy.AstString || argStrNode.Type == sleepy.AstLiteral {
								if val, ok := argStrNode.Value.(string); ok {
									for g := range localGlobals {
										val = strings.ReplaceAll(val, "$"+g, "$"+prefix+g)
										val = strings.ReplaceAll(val, "%"+g, "%"+prefix+g)
										val = strings.ReplaceAll(val, "@"+g, "@"+prefix+g)
									}
									argStrNode.Value = val
								}
							}
						}
					}
				}
			}

			return n
		})

		root.Children = append(root.Children, node.Children...)
	}

	formatted := root.Format(parser)

	if err := os.WriteFile(outputFile, []byte(formatted), 0644); err != nil {
		fmt.Printf("Error writing output file: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("\nSuccessfully bundled %d files into %s\n", len(inputFiles), outputFile)
}
