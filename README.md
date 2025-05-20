# sdn terminal

## Features

- **Command Execution**: Run standard Unix commands.
- **Pipelines**: Chain commands using `|`.
- **Redirection**: Support for input (`<`), output (`>`), and append (`>>`) redirection.
- **Background Processes**: Use `&` to run commands in the background.
- **Command History**:
  - View history with `history`.
  - Navigate history using Up/Down arrows.
  - Persistent history saved to `~/.sdn_history`.
- **Autocompletion**:
  - Tab completion for commands and filenames.
  - Displays matching filenames if multiple options exist.
- **Alias Support**:
  - Define and use aliases for commands (e.g., `alias ll="ls -al"`).
  - Manage aliases with `alias` and `unalias` commands.
- **Built-in Commands**:
  - `cd`: Change directory.
  - `exit`: Exit the shell.
  - `history`: Show command history.
- **Interactive Editing**: Backspace, arrow keys, and Ctrl+D for input control.
- **Error Handling**: Informative messages for syntax and execution errors.