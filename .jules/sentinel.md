## 2024-05-08 - Fix Command Injection in SysDialog.cpp
**Vulnerability:** The `SystemDialogs::SaveFileDialog` method concatenated an unescaped, user-controlled `preferred_name` string directly into a shell command for `zenity` and `kdialog` that was executed via `popen`, making it vulnerable to command injection.
**Learning:** File dialog wrappers or other external commands using `system()` or `popen()` must sanitize or strictly escape string parameters (e.g. by wrapping them in single quotes and safely managing nested single quotes).
**Prevention:** Implement and use a safe `escape_shell_arg` function for all shell command concatenations, or prefer native APIs over invoking external binaries via the shell.
