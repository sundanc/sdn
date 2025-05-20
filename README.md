# sdn terminal

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