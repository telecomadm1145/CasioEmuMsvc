The emulator is developed on Linux, so it's not compatible for Windows.

To compile it on Windows the following changes is required:

* `readline` and `readline-history` are required. If they're not available,
  monkey-patch the code in `src/casioemu.cpp` accordingly.
* `find` and `sed` unix tools must be available (use either `git` (heavy, about
  40MB but more useful) or Unxutils (light but unmaintained) to get the
  tools) If necessary, change the call to `find` to use absolute path.
* If the shell `sh` is not used: (`sh` of Unxutils appears to be buggy)
  * Add this command after the assignment to `FILES`:
    `FILES := $(subst \,/,$(FILES))`
  * Process substitution (backtick) must be replaced:
    * Because `pkg-config` is not available, just replace the `--cflags` calls
      by `-Ipath/to/include` and `--libs` calls by `-Lpath/to/lib -llibname`.
    * It's ok to define `RELBACK` as ``.
