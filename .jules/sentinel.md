## 2024-05-14 - Unescaped File Dialog Inputs
**Vulnerability:** Command injection vulnerability in `SaveFileDialog` due to unescaped user input being concatenated into bash commands for `zenity` and `kdialog`.
**Learning:** `std::string` concatenation for building shell commands is inherently unsafe if any variable originates from outside strictly controlled internal logic.
**Prevention:** Always escape variables passed to shell commands (e.g., using a wrapper like `escape_shell_arg` to convert inner single quotes to `'` and wrap the entirety in single quotes), or use execve-style functions that avoid the shell entirely.
