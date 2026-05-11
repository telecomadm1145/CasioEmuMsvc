## 2024-05-24 - [Fix Command Injection in File Dialogs]
**Vulnerability:** The `SaveFileDialog` command in Linux/Mac environments used `zenity` and `kdialog` strings compiled via string concatenation with unescaped filenames (e.g. `cmd = "zenity ... --filename=\"" + preferred_name + "\""`). This allowed for command injection via maliciously crafted paths.
**Learning:** `popen()` calls require strictly escaped arguments if using user-controlled input, even if inside other quotes within the string.
**Prevention:** Use an escaping function (like `escape_shell_arg` wrapping in single quotes and safely transforming inner quotes to `'\''`) before combining any variables into shell strings executed by `popen()`, `system()`, or `exec()`.
