# Sleepy C2 OPSEC Toolkit

The OPSEC toolkit is an example application demonstrating how to build a robust Operator Security (OPSEC) management system using the `sleepy` parsing library and its Abstract Syntax Tree (AST) code generation capabilities.

This toolkit addresses common problems in multi-operator red team engagements: restricting novice operators from executing commands that might burn infrastructure (like `Askcreds` or `PetitPotam`), while explicitly assigning privileges to Lead operators.

## Architecture & Components

The toolkit is split into a few core utilities:

### 1. The TUI Manager (`manager.go`)

The core user interface is built using `charmbracelet/bubbletea`. You can launch the manager, pointing it at a directory of tool sources, and use the terminal interface to:
- Browse all discovered BOFs (Beacon Object Files).
- Assign minimum Role-Based Access Control (RBAC) levels required to execute each command.
- Define user aliases and map them to their specific operator roles (`Lead`, `Operator`, `Reporter`).
- Enable or disable specific built-in Cobalt Strike functions.

### 2. The Configuration (`opsec.json`, `types.go`)

The manager saves all its routing, roles, and allowed commands to `opsec.json`. This acts as the single source of truth for the engagement's OPSEC posture and can be checked into version control for audits.

### 3. Cobalt Strike Policy Generation (`opsec_gen.go`, `gen_opsec.go`)

Instead of utilizing brittle text-templating or string concatenation to generate Cobalt Strike Aggressor scripts, the generator dynamically reads the exact rules of engagement from `opsec.json` and uses the robust `sleepy` Abstract Syntax Tree (AST) framework to programmatically build out a full `opsec.cna` script from AST Nodes.

This script enforces:
- Dynamic lookups mapping the current team server nickname (`mynick()`) to their Role.
- Automatic interception of built-in commands through Sleepy `alias` overwrites if an operator does not possess the required minimum role.
- Global variable instantiation (such as `@command_min_roles` and `%user_roles`) generated cleanly via the underlying `sleepy.c` bindings.

### 4. Patching & Scanning (`scanner.go`, `patcher.go`)

To enforce these protections deeper than simply the script level, the toolkit contains routines to scan the C-source code of BOFs to extract context and dependencies, and hypothetically inject enforcement patches directly into the payload compilation processes.

## Usage

The unified binary handles the entire workflow. You only need to point it at your source scripts:

```bash
# Build the unified binary
go build -o dist/toolkit ./cmd/toolkit

# Run the Interactive Manager (Automatically patches and scans)
./dist/toolkit manage third-party/

# Rebuild the opsec.cna script from the opsec.json config:
./dist/toolkit generate
```

The output `opsec.cna` file can then be loaded globally by the team server or individually by the operators to automatically enforce the team's operational security policy.
