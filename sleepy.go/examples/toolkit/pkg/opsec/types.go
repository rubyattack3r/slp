package opsec

type CommandInfo struct {
	Name     string `json:"name"`
	Summary  string `json:"summary"`
	Help     string `json:"help"`
	FilePath string `json:"file_path"`
}

type OpsecConfig struct {
	Commands map[string]string `json:"commands"` // name -> minRole
	Users    map[string]string `json:"users"`    // username -> role
	Builtins map[string]string `json:"builtins"` // name -> minRole
}
