package opsec

import (
	"fmt"
	"os"
	"sort"
	"strings"

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
		stmtStr := fmt.Sprintf(`%%user_roles["%s"] = "%s";`, strings.ToLower(k), config.Users[k])
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
sub get_user_role {
    local('$r');
    $r = %user_roles[lc(mynick())];
    if (!$r) { return "Blocked"; }
    return $r;
}

sub get_role_level {
    local('$r');
    $r = $1;
    if ($r eq "Admin") { return 100; }
    if ($r eq "Lead") { return 50; }
    if ($r eq "Operator") { return 10; }
    return 0;
}

sub can_execute_role {
    local('$v1 $v2');
    $v1 = get_role_level($1);
    $v2 = get_role_level($2);
    if ($v1 >= $v2) { return 1; }
    return 0;
}

sub can_execute {
    local('$cmd $min');
    $cmd = $1;
    $min = %command_min_roles[$cmd];
    if (!$min) { $min = "Operator"; }
    return can_execute_role(get_user_role(), $min);
}

alias toolkit_whoami {
    blog($1, "--- Toolkit Identity ---");
    blog($1, "Nickname: " . mynick());
    blog($1, "Processed: " . lc(mynick()));
    local('$r $l');
    $r = get_user_role();
    $l = get_role_level($r);
    blog($1, "Role: " . $r . " (Level: " . $l . ")");
    blog($1, "--- RBAC Diagnostics ---");
    blog($1, "can_execute('psx'): " . can_execute("psx"));
    blog($1, "can_execute_role('Lead', 'Operator'): " . can_execute_role("Lead", "Operator"));
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
        berror($1, "OPSEC: Permission denied for command 'CMD_NAME_PLACEHOLDER' (Min Role: MIN_ROLE_PLACEHOLDER, Current Role: " . get_user_role() . ")");
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
					// Note: The concatenation with get_user_role() is handled by the parser already in the template string
					// but we still need to replace the placeholders in the base string if any.
					// Actually, the parser will parse the whole expression.
					// Let's check how "OPSEC..." string looks like in the AST.
					// It's part of a binary operation (concat).
					if strings.Contains(fmt.Sprint(n.Value), "OPSEC: Permission denied") {
						val := fmt.Sprint(n.Value)
						val = strings.ReplaceAll(val, "CMD_NAME_PLACEHOLDER", k)
						val = strings.ReplaceAll(val, "MIN_ROLE_PLACEHOLDER", config.Commands[k])
						n.Value = val
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
	builtinMap := map[string]string{
		"shell":            "bshell",
		"run":              "brun",
		"execute-assembly": "bexecute_assembly",
		"powerpick":        "bpowerpick",
		"psinject":         "bpsinject",
		"pth":              "bpth",
		"spawn":            "bspawn",
		"spawnu":           "bspawnu",
		"inject":           "binject",
		"shinject":         "bshinject",
		"dllinject":        "bdllinject",
		"wdigest":          "bwdigest",
		"mimikatz":         "bmimikatz",
	}

	var builtinKeys []string
	for k := range config.Builtins {
		builtinKeys = append(builtinKeys, k)
	}
	sort.Strings(builtinKeys)

	for _, k := range builtinKeys {
		minRole := config.Builtins[k]
		if minRole == "" || minRole == "Reporter" {
			minRole = "Operator"
		}
		beaconFunc := builtinMap[k]
		if beaconFunc == "" {
			continue
		}

		aliasStr := fmt.Sprintf(`
alias %s {
    if (!can_execute_role(get_user_role(), "%s")) {
        berror($1, "OPSEC: Command '%s' is restricted (Min Role: %s, Current Role: " . get_user_role() . ").");
        return;
    }
    # Call the original beacon command function
    %s($1, substr($0, %d));
}`, k, minRole, k, minRole, beaconFunc, len(k)+1)

		if node, err := parser.Parse(aliasStr); err == nil && len(node.Children) > 0 {
			root.Children = append(root.Children, node.Children...)
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
