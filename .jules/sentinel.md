## 2024-06-25 - Fix command injection in Linux fallback file dialog
**Vulnerability:** Unescaped user input (`preferred_name`) was directly interpolated into shell commands (`zenity` and `kdialog`) executed via `popen` in `SystemDialogs::SaveFileDialog` when running on Linux desktop without native COM/JNI support.
**Learning:** Shell execution functions (`popen`, `system`) are inherently risky. When creating a fallback implementation for cross-platform components, verify that any data derived from user input or external configuration is securely escaped.
**Prevention:** Implement and enforce usage of a shell escaping utility (like `escape_shell_arg` replacing `'` with `'\''` inside single quotes) before passing string variables into system commands.
