package opsec

import (
	"os"
	"path/filepath"
	"strings"

	"github.com/sleepy/sleepy.go"
)

func ScanForCommands(dir string) ([]CommandInfo, error) {
	var commands []CommandInfo
	parser := sleepy.NewParser()
	defer parser.Close()

	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() || !strings.HasSuffix(info.Name(), ".cna") {
			return nil
		}

		content, err := os.ReadFile(path)
		if err != nil {
			return nil
		}

		node, err := parser.Parse(string(content))
		if err != nil {
			return nil
		}

		relPath, _ := filepath.Rel(dir, path)
		fileCmds := extractCommands(node, relPath)
		commands = append(commands, fileCmds...)

		return nil
	})

	return commands, err
}

func extractCommands(node *sleepy.Node, filePath string) []CommandInfo {
	var cmds []CommandInfo
	if node == nil {
		return cmds
	}

	// We look for beacon_command_register calls and aliases
	// beacon_command_register("name", "summary", "help")
	
	node.Walk(func(n *sleepy.Node) *sleepy.Node {
		if n.Type == sleepy.AstCall && n.Value == "beacon_command_register" {
			if len(n.Children) >= 3 {
				name := getStringValue(n.Children[0])
				summary := getStringValue(n.Children[1])
				help := getStringValue(n.Children[2])
				if name != "" {
					cmds = append(cmds, CommandInfo{
						Name:     name,
						Summary:  summary,
						Help:     help,
						FilePath: filePath,
					})
				}
			}
		}
		return n
	})

	return cmds
}

func getStringValue(n *sleepy.Node) string {
	if n == nil {
		return ""
	}
	// In Sleepy, arguments are often AstArg -> AstString
	if n.Type == sleepy.AstArg && len(n.Children) > 0 {
		return getStringValue(n.Children[0])
	}
	if n.Type == sleepy.AstString || n.Type == sleepy.AstLiteral {
		if s, ok := n.Value.(string); ok {
			return strings.Trim(s, "\"")
		}
	}
	// Handle concatenation if needed, but for now just simple strings
	return ""
}
