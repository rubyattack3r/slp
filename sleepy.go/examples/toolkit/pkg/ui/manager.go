package ui

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"toolkit/pkg/opsec"

	"github.com/charmbracelet/bubbles/table"
	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

var baseStyle = lipgloss.NewStyle().
	BorderStyle(lipgloss.NormalBorder()).
	BorderForeground(lipgloss.Color("240"))

type viewState int

const (
	viewCommands viewState = iota
	viewFilterCommand
	viewUsers
	viewAddUser
	viewBuiltins
)

type model struct {
	commandTable table.Model
	userTable    table.Model
	builtinTable table.Model
	input        textinput.Model
	filterInput  textinput.Model
	state        viewState
	commands     []opsec.CommandInfo
	roles        []string
	configPath   string
	distDir      string
	patchedDir   string

	cmdRoles      map[string]string
	userRoles     map[string]string
	builtinStatus map[string]string
	builtinList   []string
}

func initialModel(cmds []opsec.CommandInfo, distDir, patchedDir string) model {
	roles := []string{"Admin", "Lead", "Operator"}
	builtinList := []string{"shell", "run", "execute-assembly", "powerpick", "psinject", "pth", "spawn", "spawnu", "inject", "shinject", "dllinject", "wdigest", "mimikatz"}
	
	// Load existing config if available
	var config opsec.OpsecConfig
	configPath := "opsec.json"
	data, err := os.ReadFile(configPath)
	if err == nil {
		json.Unmarshal(data, &config)
	}
	if config.Commands == nil {
		config.Commands = make(map[string]string)
	}
	if config.Users == nil {
		config.Users = make(map[string]string)
	}
	if config.Builtins == nil {
		config.Builtins = make(map[string]string)
	}

	cmdRoles := make(map[string]string)
	for _, c := range cmds {
		role, ok := config.Commands[c.Name]
		if !ok || role == "" || role == "Reporter" {
			role = "Operator"
		}
		cmdRoles[c.Name] = role
	}

	userRoles := make(map[string]string)
	for u, r := range config.Users {
		if r == "Reporter" {
			continue // Filter out Reporter from UI
		}
		userRoles[u] = r
	}

	builtinStatus := make(map[string]string)
	for _, b := range builtinList {
		status, ok := config.Builtins[b]
		if !ok || status == "" || status == "Reporter" {
			status = "Operator" 
		}
		builtinStatus[b] = status
	}

	// Setup Command Table
	cmdColumns := []table.Column{
		{Title: "Command", Width: 20},
		{Title: "Min Role", Width: 15},
		{Title: "Summary", Width: 90},
	}
	cmdTable := table.New(table.WithColumns(cmdColumns), table.WithFocused(true), table.WithHeight(15))

	// Setup User Table
	userColumns := []table.Column{
		{Title: "Username", Width: 20},
		{Title: "Role", Width: 15},
	}
	userTable := table.New(table.WithColumns(userColumns), table.WithFocused(false), table.WithHeight(15))

	// Setup Builtin Table
	builtinColumns := []table.Column{
		{Title: "Built-in Command", Width: 30},
		{Title: "Status", Width: 15},
	}
	builtinTable := table.New(table.WithColumns(builtinColumns), table.WithFocused(false), table.WithHeight(15))

	// Setup styles for tables
	s := table.DefaultStyles()
	s.Header = s.Header.BorderStyle(lipgloss.NormalBorder()).BorderForeground(lipgloss.Color("240")).BorderBottom(true).Bold(false)
	s.Selected = s.Selected.Foreground(lipgloss.Color("229")).Background(lipgloss.Color("57")).Bold(false)
	cmdTable.SetStyles(s)
	userTable.SetStyles(s)
	builtinTable.SetStyles(s)

	ti := textinput.New()
	ti.Placeholder = "Username"

	fi := textinput.New()
	fi.Placeholder = "Filter commands..."
	fi.Prompt = "/ "

	m := model{
		commandTable:  cmdTable,
		userTable:     userTable,
		builtinTable:  builtinTable,
		input:         ti,
		filterInput:   fi,
		state:         viewCommands,
		commands:      cmds,
		roles:         roles,
		configPath:    configPath,
		distDir:       distDir,
		patchedDir:    patchedDir,
		cmdRoles:      cmdRoles,
		userRoles:     userRoles,
		builtinStatus: builtinStatus,
		builtinList:   builtinList,
	}

	m.updateCommandTable()
	m.updateUserTable()
	m.updateBuiltinTable()

	return m
}

func (m *model) updateCommandTable() {
	var rows []table.Row
	filterStr := strings.ToLower(m.filterInput.Value())
	
	for _, c := range m.commands {
		if filterStr != "" && !strings.Contains(strings.ToLower(c.Name), filterStr) && !strings.Contains(strings.ToLower(c.Summary), filterStr) {
			continue
		}
		
		role := m.cmdRoles[c.Name]
		summary := c.Summary
		if len(summary) > 87 {
			summary = summary[:84] + "..."
		}
		rows = append(rows, table.Row{c.Name, role, summary})
	}
	m.commandTable.SetRows(rows)
}

func (m *model) updateUserTable() {
	var rows []table.Row
	var keys []string
	for u := range m.userRoles {
		keys = append(keys, u)
	}
	sort.Strings(keys)
	for _, u := range keys {
		rows = append(rows, table.Row{u, m.userRoles[u]})
	}
	m.userTable.SetRows(rows)
}

func (m *model) updateBuiltinTable() {
	var rows []table.Row
	for _, b := range m.builtinList {
		rows = append(rows, table.Row{b, m.builtinStatus[b]})
	}
	m.builtinTable.SetRows(rows)
}

func (m model) Init() tea.Cmd {
	return nil
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmd tea.Cmd

	switch msg := msg.(type) {
	case tea.KeyMsg:
		if m.state == viewAddUser {
			switch msg.String() {
			case "enter":
				username := m.input.Value()
				if username != "" {
					m.userRoles[username] = "Operator"
					m.updateUserTable()
					m.userTable.SetCursor(len(m.userTable.Rows()) - 1)
					m.input.SetValue("")
					m.state = viewUsers
					m.userTable.Focus()
				}
			case "esc":
				m.state = viewUsers
				m.userTable.Focus()
			}
			m.input, cmd = m.input.Update(msg)
			return m, cmd
		}

		if m.state == viewFilterCommand {
			switch msg.String() {
			case "enter", "esc":
				m.state = viewCommands
				m.filterInput.Blur()
				return m, nil
			}
			m.filterInput, cmd = m.filterInput.Update(msg)
			m.updateCommandTable()
			return m, cmd
		}

		switch msg.String() {
		case "ctrl+c", "q":
			return m, tea.Quit
		case "/":
			if m.state == viewCommands {
				m.state = viewFilterCommand
				m.filterInput.Focus()
				return m, nil
			}
		case "tab":
			if m.state == viewCommands {
				m.state = viewUsers
				m.commandTable.Blur()
				m.userTable.Focus()
			} else if m.state == viewUsers {
				m.state = viewBuiltins
				m.userTable.Blur()
				m.builtinTable.Focus()
			} else if m.state == viewBuiltins {
				m.state = viewCommands
				m.builtinTable.Blur()
				m.commandTable.Focus()
			}
			return m, nil
		case "s":
			m.saveConfig()
			return m, tea.Quit
		case "a":
			if m.state == viewUsers {
				m.state = viewAddUser
				m.userTable.Blur()
				m.input.Focus()
				return m, nil
			}
		case "d":
			if m.state == viewUsers {
				if len(m.userTable.Rows()) > 0 {
					idx := m.userTable.Cursor()
					rows := m.userTable.Rows()
					username := rows[idx][0]
					delete(m.userRoles, username)
					m.updateUserTable()
					if idx >= len(m.userTable.Rows()) && len(m.userTable.Rows()) > 0 {
						m.userTable.SetCursor(len(m.userTable.Rows()) - 1)
					}
				}
			}
		case "right", "l", "left", "h", " ": // Space to toggle bools
			if m.state == viewCommands {
				if len(m.commandTable.Rows()) > 0 {
					idx := m.commandTable.Cursor()
					cmdName := m.commandTable.Rows()[idx][0]
					if msg.String() == "right" || msg.String() == "l" {
						m.cmdRoles[cmdName] = nextRole(m.cmdRoles[cmdName], m.roles)
					} else if msg.String() == "left" || msg.String() == "h" {
						m.cmdRoles[cmdName] = prevRole(m.cmdRoles[cmdName], m.roles)
					}
					m.updateCommandTable()
					m.commandTable.SetCursor(idx)
				}
			} else if m.state == viewUsers {
				if len(m.userTable.Rows()) > 0 {
					idx := m.userTable.Cursor()
					username := m.userTable.Rows()[idx][0]
					if msg.String() == "right" || msg.String() == "l" {
						m.userRoles[username] = nextRole(m.userRoles[username], m.roles)
					} else if msg.String() == "left" || msg.String() == "h" {
						m.userRoles[username] = prevRole(m.userRoles[username], m.roles)
					}
					m.updateUserTable()
					m.userTable.SetCursor(idx)
				}
			} else if m.state == viewBuiltins {
				if len(m.builtinTable.Rows()) > 0 {
					idx := m.builtinTable.Cursor()
					cmdName := m.builtinTable.Rows()[idx][0]
					if msg.String() == "right" || msg.String() == "l" {
						m.builtinStatus[cmdName] = nextRole(m.builtinStatus[cmdName], m.roles)
					} else if msg.String() == "left" || msg.String() == "h" {
						m.builtinStatus[cmdName] = prevRole(m.builtinStatus[cmdName], m.roles)
					}
					m.updateBuiltinTable()
					m.builtinTable.SetCursor(idx)
				}
			}
		}

	case tea.WindowSizeMsg:
		cmdHeight := msg.Height - 16
		if m.state == viewFilterCommand || m.filterInput.Value() != "" {
			cmdHeight -= 2 // Make room for filter bar
		}
		userHeight := msg.Height - 6
		if cmdHeight < 5 {
			cmdHeight = 5
		}
		if userHeight < 5 {
			userHeight = 5
		}
		m.commandTable.SetHeight(cmdHeight)
		m.userTable.SetHeight(userHeight)
		m.builtinTable.SetHeight(userHeight)
	}

	if m.state == viewCommands || m.state == viewFilterCommand {
		m.commandTable, cmd = m.commandTable.Update(msg)
	} else if m.state == viewUsers {
		m.userListUpdateMsgFix(msg)
		m.userTable, cmd = m.userTable.Update(msg)
	} else if m.state == viewBuiltins {
		m.userListUpdateMsgFix(msg) // same workaround
		m.builtinTable, cmd = m.builtinTable.Update(msg)
	}

	return m, cmd
}

func (m *model) userListUpdateMsgFix(msg tea.Msg) {}

func (m model) View() string {
	var s string
	switch m.state {
	case viewCommands, viewFilterCommand:
		var detail string
		if len(m.commandTable.Rows()) > 0 {
			selected := m.commandTable.SelectedRow()
			if len(selected) > 0 {
				cmdName := selected[0]
				for _, c := range m.commands {
					if c.Name == cmdName {
						detail = lipgloss.NewStyle().Width(100).Padding(0, 1).BorderStyle(lipgloss.NormalBorder()).BorderForeground(lipgloss.Color("240")).Render("Summary:\n" + c.Summary)
						break
					}
				}
			}
		}
		top := "BOF Command Manager (Commands)"
		if m.state == viewFilterCommand || m.filterInput.Value() != "" {
			top += "\n\n" + m.filterInput.View()
		}
		s = lipgloss.JoinVertical(lipgloss.Left, top, baseStyle.Render(m.commandTable.View()), detail)
	case viewUsers:
		s = lipgloss.JoinVertical(lipgloss.Left, "BOF Command Manager (Users)", baseStyle.Render(m.userTable.View()))
	case viewAddUser:
		s = "Add User:\n\n" + m.input.View() + "\n\n(enter to confirm, esc to cancel)"
	case viewBuiltins:
		s = lipgloss.JoinVertical(lipgloss.Left, "BOF Command Manager (Built-in Cobalt Strike Commands)", baseStyle.Render(m.builtinTable.View()))
	}

	help := "\n [tab] switch view | [left/right/space] toggle status | [s] save and exit | [q] quit"
	if m.state == viewFilterCommand {
		help = "\n [enter/esc] done filtering"
	} else if m.state == viewUsers {
		help = "\n [tab] switch view | [a] add user | [d] delete user | [left/right] change role | [s] save and exit | [q] quit"
	} else if m.state == viewAddUser {
		help = ""
	} else if m.state == viewCommands {
		help = "\n [tab] switch view | [/] filter | [left/right] change role | [s] save and exit | [q] quit"
	}

	return s + help + "\n"
}

func nextRole(current string, roles []string) string {
	for i, r := range roles {
		if r == current {
			return roles[(i+1)%len(roles)]
		}
	}
	return roles[0]
}

func prevRole(current string, roles []string) string {
	for i, r := range roles {
		if r == current {
			return roles[(i-1+len(roles))%len(roles)]
		}
	}
	return roles[len(roles)-1]
}

func (m model) saveConfig() {
	config := opsec.OpsecConfig{
		Commands: m.cmdRoles,
		Users:    m.userRoles,
		Builtins: m.builtinStatus,
	}

	data, _ := json.MarshalIndent(config, "", "  ")
	os.WriteFile(m.configPath, data, 0644)
	fmt.Printf("\nSaved config to %s\n", m.configPath)
	
	// Save opsec.cna in the root of the dist directory
	opsecPath := filepath.Join(m.distDir, "opsec.cna")
	opsec.GenerateOpsecCNA(config, opsecPath)
}

func Run(srcDir string) error {
	distDir := "dist"
	patchedDir := filepath.Join(distDir, "third-party-patched")
	rootCNA := filepath.Join(distDir, "root.cna")

	if err := os.MkdirAll(patchedDir, 0755); err != nil {
		return fmt.Errorf("failed to create patched directory: %v", err)
	}

	fmt.Printf("Automating workflow: Patching %s -> %s...\n", srcDir, patchedDir)
	if err := opsec.Patch(srcDir, patchedDir, rootCNA); err != nil {
		return fmt.Errorf("patching failed: %v", err)
	}

	fmt.Println("Scanning patched scripts for commands...")
	cmds, err := opsec.ScanForCommands(patchedDir)
	if err != nil {
		return fmt.Errorf("error scanning: %v", err)
	}

	p := tea.NewProgram(initialModel(cmds, distDir, patchedDir), tea.WithAltScreen())
	if _, err := p.Run(); err != nil {
		return fmt.Errorf("alas, there's been an error: %v", err)
	}
	return nil
}
