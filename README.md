# CasioEmu

An emulator and disassembler for the CASIO calculator series using the nX-U8/100 core.

## Disassembler

Each argument should have one of these two formats:

* `key=value` where `key` does not contain any equal signs.
* `path`: equivalent to `input=path`.

Supported values of `key` are:

* `input`: Path to input file. Defaults to `/dev/stdin`,
* `output`: Path to output file. Defaults to `/dev/stdout`,
* `entry`: Comma-separated list of 0-indexed indices of used (reset/interrupt) vectors. (each vector takes 2 bytes, so the address of vector with index `i` is `2*i`)
* `entry_addresses_file`: Path to a file containing entry addresses. Workaround
   for the fact that the disassembler cannot determine every dynamic function
   call. All lines should either be empty, has a comment (any character can be
   used to start a comment - `#`, `;` or `--` are all fine), or start with a
   hexadecimal value without prefix '0x' indicates the address.
* `complement_entries`: Specify that the list of entries (above) should be inverted in range [1..127].
* `strict`: Raises error instead of warnings when unknown instructions are encountered or jump to addresses exceed the ROM size.
* `addresses`: Specify that each line should have a comment containing the address and source bytes. `value` is not important.
* `rom_window`: Size of the ROM window. For example `0x8000`.
* `names`: Path to a file containing label names.
   Each line should either be a comment, empty,
   or starts with `generated_label_name real_label_name`.

## Emulator

### Supported models

* fx-570ES PLUS

### Command-line arguments

Each argument should have one of these two formats:

* `key=value` where `key` does not contain any equal signs.
* `path`: equivalent to `model=path`.

Supported values of `key` are: (if `value` is not mentioned then it does not matter)

* `paused`: Pause the emulator on start. Note that this causes the emulator to not render correctly.
* `model`: Specify the path to model folder. Example `value`: `models/fx570esplus`.
* `ram`: Load RAM dump from the path specified in `value`.
* `clean_ram`: If `ram` is specified, this prevents the calculator from loading the file, instead starting from a *clean* RAM state.
* `preserve_ram`: Specify that the RAM should **not** be dumped (to the value associated with the `ram` key) on program exit, in other words, *preserve* the existing RAM dump in the file.
* `strict_memory`: Pause the emulator if the program attempt to write to unwritable memory regions corresponding to ROM. (writing to unmapped memory regions does not pause the program)
* `script`: Specify a path to Lua file to be executed on program startup (using `value` parameter).

### Available Lua functions

Those Lua functions and variables can be used at the Lua prompt of the emulator.

* `printf()`: Print with format.
* `ins()`: Log all register values to the screen.
* `break_at`: Set breakpoint. If called with no arguments, break at current address.
* `unbreak_at`: Delete breakpoint. If input not specified, delete breakpoint at current address. Have no effect if there are no breakpoints at specified position.
* `cont()`: Continue program execution.

* `emu:set_paused`: Set emulator state. Call with a boolean value.
* `emu:tick()`: Execute one command.
* `emu:shutdown()`: Shutdown the emulator.

* `cpu.xxx`: Get register value. `xxx` should be one of
	* `r0` to `r15`
	* One of the register names specified in `CPU.cpp:200..216`
	* `erN`, `xrN`, `qrN` are **not** supported.
* `cpu.bt`: A string containing the current stack trace.

* `code[address]`: Access code. (By words, only use even address, otherwise program will panic)
* `data[address]`: Access data. (By bytes)
* `data:watch(offset, fn)`: Set watchpoint at address `offset` - `fn` is called whenever
data is written to. If `fn` is `nil`, clear the watchpoint.
* `data:rwatch(offset, fn)`: Set watchpoint at address `offset` - `fn` is called whenever
data is read from as data. If `fn` is `nil`, clear the watchpoint.

### Build

Run `make` in the `emulator` folder. Dependencies: (listed in the `Makefile`)

* Lua 5.3 (note: the include files should be put in `lua5.3` folder, otherwise change the `#include` lines accordingly)
* SDL2
* SDL2\_image
* pthread (already available for UNIX systems)

### Usage

Run the generated executable at `emulator/bin/casioemu`.

To interact with the calculator keyboard, use the mouse (left click to press, right click to stick) or the keyboard (see `models/*/model.lua` for keyboard configuration).
