# vin - Lightweight Terminal Text Editor

A custom text editor built in C with a focus on simplicity and efficiency.

Created by Vincent Welbourne (vincent.vw04@gmail.com)

## Features

- **Gap Buffer Implementation** - Efficient text editing data structure
- **File Operations** - Load, edit, and save files seamlessly
- **Undo/Redo** - Full history tracking with Ctrl-U / Ctrl-R
- **Search** - Find text with Ctrl-F, navigate with n/N
- **Line Numbers** - Toggle line numbers on/off
- **Customizable** - Remap keybindings via config file
- **Terminal Resize Support** - Adapts to window size changes
- **Status Messages** - Real-time feedback on operations

## Installation

### Prerequisites

- Linux or Unix-like system
- GCC compiler
- Make

### Quick Install (System-wide)
```bash
git clone https://github.com/bicente44/vin.git
cd vin
make
sudo make install
```

The editor will be installed to `/usr/local/bin/vin` and a default config file will be created at `~/.vinrc`.

### User Install (No sudo required)
```bash
git clone https://github.com/bicente44/vin.git
cd vin
make
make install-user
```

Then add `~/.local/bin` to your PATH:
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Building Only (No Install)
```bash
make
./bin/vin filename
```

## Usage
```bash
# Open or create a file
vin filename.txt

# Open with no file (will prompt for filename on save)
vin

# Show help
vin --help

# Show version
vin --version
```

## Keybindings

### File Operations
- **Ctrl-S** - Save file
- **Ctrl-Q** - Quit (prompts if unsaved changes)

### Editing
- **Ctrl-U** - Undo
- **Ctrl-R** - Redo
- **Ctrl-D** - Delete entire line
- **Backspace** - Delete character before cursor
- **Delete** - Delete character at cursor
- **Enter** - Insert newline

### Navigation
- **Arrow Keys** - Move cursor
- **Ctrl-G** - Jump to top of file
- **Ctrl-B** - Jump to bottom of file
- **Home** - Jump to start of line
- **End** - Jump to end of line
- **Page Up** - Scroll up one screen
- **Page Down** - Scroll down one screen

### Search
- **Ctrl-F** - Start search (type query, press Enter)
- **n** - Find next match
- **N** - Find previous match
- **ESC** - Exit search mode

## Configuration

Customize vin by editing `~/.vinrc`. Example:
```ini
# Display settings
line_numbers=on
tab_width=4
status_timeout=2

# Keybindings (remap as desired)
quit=CTRL_Q
save=CTRL_S
undo=CTRL_U
redo=CTRL_R
search=CTRL_F
delete_line=CTRL_D
jump_top=CTRL_G
jump_bottom=CTRL_B
```

Available control keys: `CTRL_A` through `CTRL_Z`

## Uninstalling

### System-wide installation
```bash
sudo make uninstall
```

### User installation
```bash
make uninstall-user
```

The config file (`~/.vinrc`) is preserved during uninstall.

## Architecture

- gapbuffer.c - Gap buffer data structure for efficient insertions/deletions
- terminal.c - Raw terminal mode and window management
- input.c - Keyboard input parsing and escape sequences
- screen.c - Rendering, cursor management, and editor state
- fileio.c - File loading and saving operations
- main.c - Entry point and initialization

### Future Enhancements
- [ ] Syntax highlighting
- [ ] Copy/paste with visual selection
- [ ] UTF-8 support
- [ ] Tab handling improvements
- [ ] Multiple file buffers
- [ ] Split window view

## Development

### Building
```bash
make                 # Build the editor
make clean           # Remove build files
make run FILE=test   # Run on a test file
```

## License

This project is open source and available for educational purposes.
