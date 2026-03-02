package opsec

import (
	"fmt"
	"os"
	"sort"

	"github.com/sleepy/sleepy.go"
)

func GenerateOpsecCNA(config OpsecConfig, outputPath string) error {
	parser := sleepy.NewParser()
	defer parser.Close()

	root := &sleepy.Node{Type: sleepy.AstScript}

	// 1. Roles map
	var userKeys []string
	for k := range config.Users {
		userKeys = append(userKeys, k)
	}
	sort.Strings(userKeys)

	// Since AstKvPair is supported but AstHashtable formatting is a bit weird,
	// we initialize empty map, then add indices manually:
	// %user_roles = %();
	rolesInit, _ := parser.Parse("%user_roles = %();")
	root.Children = append(root.Children, rolesInit.Children...)

	for _, k := range userKeys {
		stmtStr := fmt.Sprintf(`%%user_roles["%s"] = "%s";`, k, config.Users[k])
		if node, err := parser.Parse(stmtStr); err == nil {
			root.Children = append(root.Children, node.Children...)
		}
	}

	// 2. Commands map
	var cmdKeys []string
	for k := range config.Commands {
		cmdKeys = append(cmdKeys, k)
	}
	sort.Strings(cmdKeys)

	cmdsInit, _ := parser.Parse("%command_min_roles = %();")
	root.Children = append(root.Children, cmdsInit.Children...)

	for _, k := range cmdKeys {
		stmtStr := fmt.Sprintf(`%%command_min_roles["%s"] = "%s";`, k, config.Commands[k])
		if node, err := parser.Parse(stmtStr); err == nil {
			root.Children = append(root.Children, node.Children...)
		}
	}

	// 3. can_execute block
	canExec := `
$user_role_global = %user_roles[mynick()];
if (!$user_role_global) {
    $user_role_global = "Reporter";
}

sub can_execute_role {
    local('$r $min_role');
    $r = $1;
    $min_role = $2;
    if ($r eq "Reporter") { return $false; }
    if ($r eq "Lead") { return $true; }
    if ($r eq "Operator") {
        if ($min_role eq "Operator" || $min_role eq "Reporter") {
            return $true;
        }
    }
    return $false;
}

sub can_execute {
    local('$cmd $min_role');
    $cmd = $1;
    $min_role = %command_min_roles[$cmd];
    return can_execute_role($user_role_global, $min_role);
}
`
	if node, err := parser.Parse(canExec); err == nil && len(node.Children) > 0 {
		root.Children = append(root.Children, node.Children...)
	} else if err != nil {
		fmt.Printf("Error parsing can_execute: %v\n", err)
	}

	// 4. Wrappers
	for _, k := range cmdKeys {
		aliasStr := `
alias CMD_NAME_PLACEHOLDER {
    if (!can_execute("CMD_NAME_PLACEHOLDER")) {
        berror($1, "OPSEC: Permission denied for command 'CMD_NAME_PLACEHOLDER' (Min Role: MIN_ROLE_PLACEHOLDER)");
        return;
    }
    TOOLKIT_PLACEHOLDER($1, substr($0, SUBSTR_PLACEHOLDER));
}`
		node, err := parser.Parse(aliasStr)
		if err == nil && len(node.Children) > 0 {
			// Walk over AST to replace placeholders conceptually
			node.Walk(func(n *sleepy.Node) *sleepy.Node {
				switch n.Type {
				case sleepy.AstIdentifier:
					if n.Value == "CMD_NAME_PLACEHOLDER" {
						n.Value = k
					}
					if n.Value == "MIN_ROLE_PLACEHOLDER" {
						n.Value = config.Commands[k]
					}
				case sleepy.AstString:
					if n.Value == `"CMD_NAME_PLACEHOLDER"` {
						n.Value = fmt.Sprintf(`"%s"`, k)
					}
					if n.Value == `"OPSEC: Permission denied for command 'CMD_NAME_PLACEHOLDER' (Min Role: MIN_ROLE_PLACEHOLDER)"` {
						n.Value = fmt.Sprintf(`"OPSEC: Permission denied for command '%s' (Min Role: %s)"`, k, config.Commands[k])
					}
				case sleepy.AstCall, sleepy.AstEnvBridge: // In sleepy, env bridge identifiers are part of the bridge, but parser sets them dynamically? Wait! Toolkit calls are AstCall targets
					if str, ok := n.Value.(string); ok {
						if str == "TOOLKIT_PLACEHOLDER" {
							n.Value = "toolkit_" + k
						} else if str == "CMD_NAME_PLACEHOLDER" {
							n.Value = k
						}
					}
				case sleepy.AstNumber:
					if n.Value == float64(0) { // SUBSTR_PLACEHOLDER isn't number parsed, it's identifier
						// handled below
					}
				}
				// Also check if toolkit_placeholder is identifier
				if n.Type == sleepy.AstIdentifier && n.Value == "SUBSTR_PLACEHOLDER" {
					n.Type = sleepy.AstNumber
					n.Value = float64(len(k) + 1)
				}
				return n
			})
			root.Children = append(root.Children, node.Children...)
		} else if err != nil {
			fmt.Printf("Error parsing alias wrapper: %v\n", err)
		}
	}

	// Builtin wrappers
	var builtinKeys []string
	for k := range config.Builtins {
		builtinKeys = append(builtinKeys, k)
	}
	sort.Strings(builtinKeys)

	for _, k := range builtinKeys {
		minRole := config.Builtins[k]
		if minRole == "" {
			minRole = "Lead"
		}

		aliasStr := `
if (!can_execute_role($user_role_global, "MIN_ROLE_PLACEHOLDER")) {
    alias CMD_NAME_PLACEHOLDER {
        berror($1, "OPSEC_DENIED_PLACEHOLDER");
        return;
    }
}`
		node, err := parser.Parse(aliasStr)
		if err == nil && len(node.Children) > 0 {
			node.Walk(func(n *sleepy.Node) *sleepy.Node {
				if n.Type == sleepy.AstIdentifier && n.Value == "CMD_NAME_PLACEHOLDER" {
					n.Value = k
				}
				if n.Type == sleepy.AstString {
					if n.Value == `"MIN_ROLE_PLACEHOLDER"` {
						n.Value = fmt.Sprintf(`"%s"`, minRole)
					} else if n.Value == `"OPSEC_DENIED_PLACEHOLDER"` {
						n.Value = fmt.Sprintf(`"OPSEC: Command '%s' is restricted (Min Role: %s)."`, k, minRole)
					}
				}
				return n
			})
			root.Children = append(root.Children, node.Children...)
		} else if err != nil {
			fmt.Printf("Error parsing builtin wrapper: %v\n", err)
		}
	}

	f, err := os.Create(outputPath)
	if err != nil {
		return err
	}
	defer f.Close()

	f.WriteString("# OPSEC Enforcement Script\n")
	f.WriteString("# Generated by Sleepy BOF Manager\n\n")

	f.WriteString(root.Format(parser))

	return nil
}
