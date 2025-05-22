# SDN Terminal

A custom terminal emulator with an integrated shell.

## Features

- **Command Execution**: Run standard Unix commands.
- **Pipelines**: Chain commands using `|`.
- **Redirection**: Support for input (`<`), output (`>`), and append (`>>`) redirection.
- **Background Processes**: Use `&` to run commands in the background. Shell notifies when background processes complete.
- **Command History**:
  - View history with `history`. Entries are timestamped.
  - Navigate history using Up/Down arrows.
  - Persistent history saved to `~/.sdn_history`.
- **Autocompletion**:
  - Tab completion for commands (with inline suggestions) and filenames/directories.
  - Completes to the longest common prefix for multiple file/directory matches.
  - Displays matching filenames/directories if multiple options exist after a Tab press.
- **Wildcard Expansion (Globbing)**: Supports `*`, `?`, `[]`, and `{}` patterns for filename expansion in command arguments.
- **Alias Support**:
  - Define and use aliases for commands (e.g., `alias ll="ls -al"`).
  - Manage aliases with `alias` and `unalias` commands.
- **Directory-Local Aliases**: Automatically loads aliases from a `.sdn_local_aliases` file in the current directory when you `cd` into it. These aliases are cleared when you `cd` out. This allows for project-specific command shortcuts.
- **Built-in Commands**:
  - `cd`: Change directory.
  - `exit`: Exit the shell.
  - `history`: Show command history.
- **Interactive Editing**: Backspace, arrow keys, and Ctrl+D for input control.
- **Error Handling**: Informative messages for syntax and execution errors.
- **Terminal Features**:
  - Copy/Paste with Ctrl+Shift+C and Ctrl+Shift+V
  - Customizable colors via command-line themes (dark (default), light, light-blue, gray)
  - 10,000 lines of scrollback buffer

## Building and Installation

### Dependencies

The terminal requires the following libraries:
- GTK+ 3.0
- VTE 2.91 (Virtual Terminal Emulator)

On Ubuntu/Debian, install dependencies with:
```bash
sudo apt-get install libgtk-3-dev libvte-2.91-dev
```

On Fedora:
```bash
sudo dnf install gtk3-devel vte291-devel
```

On Arch Linux:
```bash
sudo pacman -S gtk3 vte3
```

### Building

Use the included Makefile:

```bash
make
```

This builds both the `sdn` shell and the `sdn_terminal` application.

### Installation

To install the applications to your user directory:

```bash
make install
```

This installs to `~/.local/bin/` and adds a desktop entry.

## Running

You can run the terminal emulator in several ways:

1. From the build directory:
   ```bash
   ./sdn_terminal
   ```

2. If installed:
   ```bash
   sdn_terminal
   # To use a specific theme, for example "light":
   sdn_terminal --theme light
   # Available themes: dark(default), light, light-blue, gray
   ```

3. From your desktop environment's application menu (search for "SDN Terminal")
   
   **Note:** The desktop entry (`sdn.desktop`) uses the `Exec` field to launch `sdn_terminal` from your user install location. 
   If you are installing from a release archive (e.g, `sdn-0.1.tar.gz`), the `sdn.desktop` file within the archive will have a placeholder username in the `Exec` path (e.g., `Exec=/home/your_username/.local/bin/sdn_terminal`). 
   You **must** manually edit this file after installation (usually found in `~/.local/share/applications/sdn.desktop`) and replace `your_username` with your actual username for the desktop entry to work correctly.
   By default, if you build and install from source using `make install`, the `Exec` path in the `sdn.desktop` file copied to `~/.local/share/applications/` should reflect the username of the user who ran the install command. However, always verify this path if the desktop entry doesn't work.

## Using Just the Shell

If you want to use the SDN shell without the terminal emulator:

```bash
./sdn
```

Or if installed:
```bash
sdn
```

## Uninstallation

To uninstall:

```bash
make uninstall
```

## Keyboard Shortcuts

- `Ctrl+Shift+C`: Copy selected text
- `Ctrl+Shift+V`: Paste from clipboard
- `Ctrl+D`: Exit the shell