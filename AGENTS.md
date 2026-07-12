# Repository Instructions

- Treat text files in this repository as UTF-8 without BOM unless a file clearly indicates otherwise.
- When reading files from PowerShell, do not rely on the shell's default encoding. Use an explicit UTF-8 read, for example:
  `[System.IO.File]::ReadAllText($path, [System.Text.UTF8Encoding]::new($false, $true))`.
- When writing files from PowerShell, preserve UTF-8 without BOM. In PowerShell 7+, use `-Encoding utf8NoBOM`; in Windows PowerShell 5.1, use `.NET` APIs with `[System.Text.UTF8Encoding]::new($false)`.
- If non-ASCII text appears garbled, re-read the file explicitly as UTF-8 without BOM before drawing conclusions or editing it.
