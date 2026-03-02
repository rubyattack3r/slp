package opsec

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/sleepy/sleepy.go"
)

func Patch(srcDir, outDir, rootFile string) error {
	if srcDir == "" || outDir == "" {
		return fmt.Errorf("srcDir and outDir are required")
	}

	finalRootPath := rootFile
	if finalRootPath == "" {
		finalRootPath = filepath.Join(outDir, "root.cna")
	}

	var scripts []string

	err := filepath.Walk(srcDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(srcDir, path)
		if err != nil {
			return err
		}

		targetPath := filepath.Join(outDir, relPath)

		if info.IsDir() {
			return os.MkdirAll(targetPath, 0755)
		}

		if strings.HasSuffix(path, ".cna") {
			fmt.Printf("Patching %s...\n", relPath)
			parser := sleepy.NewParser()
			defer parser.Close()

			if err := patchScript(parser, path, targetPath); err != nil {
				return fmt.Errorf("failed to patch %s: %v", path, err)
			}
			scripts = append(scripts, relPath)
		} else {
			fmt.Printf("Copying %s...\n", relPath)
			if err := copyFile(path, targetPath); err != nil {
				return fmt.Errorf("failed to copy %s: %v", path, err)
			}
		}
		return nil
	})

	if err != nil {
		return fmt.Errorf("error during walk: %v", err)
	}

	if err := generateRootCNA(finalRootPath, outDir, scripts); err != nil {
		return fmt.Errorf("error generating root.cna: %v", err)
	}

	fmt.Println("Patcher completed successfully!")
	return nil
}

func patchScript(parser *sleepy.Parser, srcPath, dstPath string) error {
	content, err := os.ReadFile(srcPath)
	if err != nil {
		return err
	}

	node, err := parser.Parse(string(content))
	if err != nil {
		return err
	}

	// Transform the AST
	node = node.Walk(func(n *sleepy.Node) *sleepy.Node {
		if n == nil {
			return nil
		}

		if n.Type == sleepy.AstEnvBridge && n.Value == "alias" {
			// Find the identifier child and rename it
			for _, child := range n.Children {
				if child.Type == sleepy.AstIdentifier {
					if name, ok := child.Value.(string); ok {
						child.Value = "toolkit_" + name
					}
					break
				}
			}
		}

		if n.Type == sleepy.AstCall {
			switch n.Value {
			case "openf":
				n.Value = "toolkit_openf"
			case "script_resource":
				n.Value = "toolkit_resource"
			case "include":
				// For include, we might want to ensure the path is consistent with the new structure
				// But since we preserve the directory structure, it should stay mostly the same
				// except maybe if it was double-dot relative.
				// For now, we'll keep it as is unless we find issues.
			}
		}

		return n
	})

	formatted := node.Format(parser)
	return os.WriteFile(dstPath, []byte(formatted), 0644)
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}

func generateRootCNA(rootPath, outDir string, scripts []string) error {
	f, err := os.Create(rootPath)
	if err != nil {
		return err
	}
	defer f.Close()

	// Calculate relative path from rootPath's directory to outDir
	rootParent := filepath.Dir(rootPath)
	relToOut, err := filepath.Rel(rootParent, outDir)
	if err != nil {
		return err
	}

	// Define toolkit functions
	header := `
# Toolkit Functions
# Set $toolkit_location to the base directory of the toolkit (e.g. file share or local path)
$toolkit_location = script_resource("");

sub toolkit_openf {
    local('$path');
    $path = $1;
    if ($toolkit_location) {
        return openf($toolkit_location . "/" . $path);
    }
    return openf($path);
}

sub toolkit_resource {
    local('$path');
    $path = $1;
    if ($toolkit_location) {
        return $toolkit_location . "/" . $path;
    }
    return script_resource($path);
}

# Include the generated OPSEC enforcement script
include(toolkit_resource("opsec.cna"));

`
	if _, err := f.WriteString(header); err != nil {
		return err
	}

	// Include all scripts
	for _, script := range scripts {
		// The script paths are relative to outDir. 
		// We need to make them relative to rootParent.
		fullScriptPath := filepath.Join(relToOut, script)
		// Use forward slashes for cross-platform compatibility in CNA includes
		fullScriptPath = filepath.ToSlash(fullScriptPath)
		if _, err := f.WriteString(fmt.Sprintf("include(toolkit_resource(\"%s\"));\n", fullScriptPath)); err != nil {
			return err
		}
	}

	return nil
}
