package main

import (
	"fmt"
	"os"
	"encoding/json"

	"toolkit/pkg/opsec"
	"toolkit/pkg/ui"
)

func printUsage() {
	fmt.Println("Sleepy OPSEC Toolkit")
	fmt.Println("Usage: toolkit <subcommand> [args...]")
	fmt.Println("\nSubcommands:")
	fmt.Println("  manage <src_dir>                         - Patch sources and launch the interactive manager UI")
	fmt.Println("  patch <src_dir> <out_dir> [root_file]    - Patch sleep scripts with toolkit wrapper aliases")
	fmt.Println("  generate                                 - Generate opsec.cna from opsec.json")
}

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	switch os.Args[1] {
	case "manage":
		if len(os.Args) < 3 {
			fmt.Println("Usage: toolkit manage <src_dir>")
			os.Exit(1)
		}
		if err := ui.Run(os.Args[2]); err != nil {
			fmt.Printf("Manager error: %v\n", err)
			os.Exit(1)
		}
	case "patch":
		if len(os.Args) < 4 {
			fmt.Println("Usage: toolkit patch <src_dir> <out_dir> [root_file]")
			os.Exit(1)
		}
		rootFile := ""
		if len(os.Args) > 4 {
			rootFile = os.Args[4]
		}
		if err := opsec.Patch(os.Args[2], os.Args[3], rootFile); err != nil {
			fmt.Printf("Patcher error: %v\n", err)
			os.Exit(1)
		}
	case "generate":
		configPath := "opsec.json"
		data, err := os.ReadFile(configPath)
		if err != nil {
			fmt.Printf("Error reading %s: %v\n", configPath, err)
			os.Exit(1)
		}

		var config opsec.OpsecConfig
		if err := json.Unmarshal(data, &config); err != nil {
			fmt.Printf("Error parsing %s: %v\n", configPath, err)
			os.Exit(1)
		}

		// For the manual generate command, output to dist/opsec.cna
		outPath := "dist/opsec.cna"
		if err := opsec.GenerateOpsecCNA(config, outPath); err != nil {
			fmt.Printf("Error generating opsec.cna: %v\n", err)
			os.Exit(1)
		}
		fmt.Printf("Successfully generated %s!\n", outPath)
	default:
		fmt.Printf("Unknown subcommand: %s\n", os.Args[1])
		printUsage()
		os.Exit(1)
	}
}
