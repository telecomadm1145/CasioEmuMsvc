## 2024-05-08 - Fix Command Injection in SysDialog.cpp
**Vulnerability:** The `SystemDialogs::SaveFileDialog` method concatenated an unescaped, user-controlled `preferred_name` string directly into a shell command for `zenity` and `kdialog` that was executed via `popen`, making it vulnerable to command injection.
**Learning:** File dialog wrappers or other external commands using `system()` or `popen()` must sanitize or strictly escape string parameters (e.g. by wrapping them in single quotes and safely managing nested single quotes).
**Prevention:** Implement and use a safe `escape_shell_arg` function for all shell command concatenations, or prefer native APIs over invoking external binaries via the shell.
## 2024-05-09 - Fix Path Traversal (Zip Slip) in RomPackage.h
**Vulnerability:** The `RomPackage::ExtractTo` method extracted ROM package contents directly using paths defined within the package metadata (`ModelInfo.rom_path`, `ModelInfo.flash_path`, `ModelInfo.interface_path`). A maliciously crafted `.rompackage` file could use absolute paths or relative paths with `..` to perform path traversal and write files outside the intended extraction directory.
**Learning:** During extraction processes, all paths originating from potentially untrusted external metadata must be explicitly checked to ensure they resolve within the intended destination bounds. Using standard `operator/` with `std::filesystem::path` is unsafe if the right-hand operand is absolute.
**Prevention:** Implement an `isPathSafe` validator that rejects absolute paths and paths containing `..` components before appending them to the base extraction directory.
## 2024-05-10 - Fix Buffer Overflows in ImGui Text Inputs
**Vulnerability:** Unbounded `sprintf` was used to write formatted data into fixed-size ImGui text buffers (e.g., `char AddrInputBuf[32]`) in components like `hex.hpp`, `BitmapViewer.cpp`, and `LabelViewer.cpp`, posing a potential buffer overflow risk if formatted data exceeded the buffer size.
**Learning:** ImGui text input fields typically rely on fixed-size char arrays. Using `sprintf` with these buffers is inherently unsafe and a common anti-pattern in C++ UI code.
**Prevention:** Always use bounds-checked `snprintf` in conjunction with `sizeof(buf)` when formatting strings into fixed-size ImGui buffers to ensure safe truncation and null-termination.
