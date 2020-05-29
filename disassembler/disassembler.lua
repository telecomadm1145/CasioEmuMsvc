#! /usr/bin/env luajit

loadstring = loadstring or load
unpack = unpack or table.unpack

if not bit then
	-- First, try using Lua 5.3's bitwise operators implementation.
	f = loadstring [[
		bit = {}
		function bit.band(a, b)   return a & b  end
		function bit.bor(a, b)    return a | b  end
		function bit.bxor(a, b)   return a ~ b  end
		function bit.lshift(a, b) return a << b end
		function bit.rshift(a, b) return a >> b end
	]]
	if f ~= nil then
		f()
	end
end

if not bit then
	bit = {}

	local function normalize(r)
		r = r % 0x100000000
		if r >= 0x80000000 then
			r = r - 0x100000000
		end
		return r
	end

	local function loopfunc(a, b, t, u)
		a = a % 0x100000000
		b = b % 0x100000000
		local r = 0
		for ix = 31, 0, -1 do
			local v = 2 ^ ix
			local m = 0
			if a >= v then
				a = a - v
				m = m + 1
			end
			if b >= v then
				b = b - v
				m = m + 1
			end
			if m == t or m == u then
				r = r + v
			end
		end
		return normalize(r)
	end

	function bit.band(a, b)
		return loopfunc(a, b, 2)
	end

	function bit.bor(a, b)
		return loopfunc(a, b, 1, 2)
	end

	function bit.bxor(a, b)
		return loopfunc(a, b, 1)
	end

	function bit.lshift(a, b)
		return normalize(a * 2 ^ b)
	end

	function bit.rshift(a, b)
		return normalize(math.floor((a % 0x100000000) / 2 ^ b))
	end

	function bit.arshift(a, b) -- for completeness?
		return math.floor(normalize(a) / 2 ^ b)
	end
end

local args_assoc = {
	input = "/dev/stdin", -- Path to input file
	output = "/dev/stdout", -- Path to output file
	-- Comma-separated list of 0-indexed indices of used (reset/interrupt)
	-- vectors (each vector takes 2 bytes, so the address of vector with index
	-- `i` is `2*i`)
	entry = "",
	-- The list of entries (above) should be inverted in range [1..127]
	complement_entries = false,
	-- Raises error instead of warnings when unknown instructions are
	-- encountered or jump to addresses exceed the ROM size.
	strict = false,
	-- Each line should have a comment containing the address and source bytes
	addresses = false,
	-- Add comments for word-manipulation commands.
	-- Example:
	--   mov r0, 1
	--   mov r1, 1
	-- and
	--   mov er0, 257
	-- are equivalent (but the latter is not valid assembly code)
	word_commands = false,
	rom_window = 0, -- Size of the ROM window. For example `0x8000`
	-- Path to a file containing label names.
	-- Each line should either be a comment, empty, or starts with
	-- `raw_label_name real_label_name` (`real_label_name` may be empty).
	-- `raw_label_name` may have one of the following formats:
	-- * A global label `f_01234`
	-- * A local label `.l_5`
	-- * A global label followed by a local label `f_01234.l_5`
	-- * An address - a hex number without leading `0x`. In that case, it's
	-- considered a possible address for the code to reach - this is necessary
	-- because the disassembler cannot resolve all variable branches/function
	-- calls.
	names = nil,
}
-- For boolean arguments, any value provided is considered 'true'

for ix, arg in next, {...} do
	local key, value = arg:match("^([^=]+)=(.+)$")
	if key then
		args_assoc[key] = value
	else
		args_assoc.input = arg
	end
end

args_assoc.rom_window = tonumber(args_assoc.rom_window)

local DATA_LABEL_FORMAT = "d_%05X"
local GLOBAL_LABEL_FORMAT = "f_%05X"
local LOCAL_LABEL_FORMAT = ".l_%03X"

local function print(thing, ...)
	io.stderr:write(tostring(thing))
	if ... then
		io.stderr:write(", ")
		print(...)
	else
		io.stderr:write("\n")
	end
end

local function printf(...)
	print(string.format(...))
end

local function panic(...)
	io.stderr:write("PANIC: ")
	printf(...)
	os.exit(1)
end

local function panic2(...)
	io.stderr:write("PANIC: ")
	printf(...)
	if args_assoc.strict then
		os.exit(1)
	end
end

local handle = io.open(args_assoc.input, "rb")
if not handle then
	panic("Failed to open \"%s\"", args_assoc.input)
end
local binary_source = handle:read("*a")
handle:close()

local binary_source_length = #binary_source
printf("Read %i bytes", binary_source_length)

local formats
do
	local condition_names = {"ge", "lt", "gt", "le", "ges", "lts", "gts", "les", "ne", "eq", "nv", "ov", "ps", "ns", "al"}
	formats = {
		[ "lab"] = {format = function(self, value, instr)
			if instr.context ~= value.context and not value.context_head and
				value.context and value.name:sub(1, 1) == '.' then
				return value.context.name .. value.name
			end
			return value.name
		end},
		["dlab"] = {format = function(self, value, instr)
			if value.ref_instr then
				return ("%s+%i"):format(formats.lab:format(value.ref_instr.under_label, instr), value.address - value.ref_instr.under_label.address)
			end
			return value.name
		end},
		[ "str"] =    "%s",
		[   "r"] =   "r%i",
		[  "er"] =  "er%i",
		[  "xr"] =  "xr%i",
		[  "qr"] =  "qr%i",
		[  "cr"] =  "cr%i",
		[ "cer"] = "cer%i",
		[ "cxr"] = "cxr%i",
		[ "cqr"] = "cqr%i",
		[ "dsr"] =   "dsr",
		[  "ea"] =    "ea",
		[ "eap"] =   "ea+",
		[  "im"] =    "%i",
		[  "bo"] =    "%i",
		[ "elr"] =   "elr",
		[  "lr"] =    "lr",
		[ "psw"] =   "psw",
		["epsw"] =  "epsw",
		["ecsr"] =  "ecsr",
		[  "sp"] =    "sp",
		[  "pc"] =    "pc",
		[  "co"] = {format = function(self, value)
			return condition_names[value + 1]
		end},
		[  "jo"] = {format = function(self, value, instr)
			return ("%i"):format((instr.address + instr.length + value * 2) % 0x10000)
		end}
	}
end

print("Generating instruction lookup table...")
local instruction_lookup = {}
do
	local instruction_source = {
		{{  "add", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8001, false, 0},
		{{  "add", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x1000, false, 0},
		{{  "add", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}                     }, 0xF006, false, 0},
		{{  "add", {  "er",  8, 0x000E}, {  "im",  0, -0x007F}                     }, 0xE080, false, 0},
		{{ "addc", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8006, false, 0},
		{{ "addc", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x6000, false, 0},
		{{  "and", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8002, false, 0},
		{{  "and", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x2000, false, 0},
		{{  "cmp", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8007, false, 0},
		{{  "cmp", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x7000, false, 0},
		{{ "cmpc", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8005, false, 0},
		{{ "cmpc", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x5000, false, 0},
		{{  "mov", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}                     }, 0xF005, false, 0},
		{{  "mov", {  "er",  8, 0x000E}, {  "im",  0, -0x007F}                     }, 0xE000, false, 0},
		{{  "mov", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8000, false, 0},
		{{  "mov", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x0000, false, 0},
		{{   "or", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8003, false, 0},
		{{   "or", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x3000, false, 0},
		{{  "xor", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8004, false, 0},
		{{  "xor", {   "r",  8, 0x000F}, {  "im",  0,  0x00FF}                     }, 0x4000, false, 0},
		{{  "cmp", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}                     }, 0xF007, false, 0},
		{{  "sub", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8008, false, 0},
		{{ "subc", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x8009, false, 0},
		{{  "sll", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x800A, false, 0},
		{{  "sll", {   "r",  8, 0x000F}, {  "im",  4,  0x0007}                     }, 0x900A, false, 0},
		{{ "sllc", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x800B, false, 0},
		{{ "sllc", {   "r",  8, 0x000F}, {  "im",  4,  0x0007}                     }, 0x900B, false, 0},
		{{  "sra", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x800E, false, 0},
		{{  "sra", {   "r",  8, 0x000F}, {  "im",  4,  0x0007}                     }, 0x900E, false, 0},
		{{  "srl", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x800C, false, 0},
		{{  "srl", {   "r",  8, 0x000F}, {  "im",  4,  0x0007}                     }, 0x900C, false, 0},
		{{ "srlc", {   "r",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0x800D, false, 0},
		{{ "srlc", {   "r",  8, 0x000F}, {  "im",  4,  0x0007}                     }, 0x900D, false, 0},
		{{    "l", {  "er",  8, 0x000E}, {  "ea",  0,  0x0000}                     }, 0x9032, false, 2},
		{{    "l", {  "er",  8, 0x000E}, { "eap",  0,  0x0000}                     }, 0x9052, false, 2},
		{{    "l", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}                     }, 0x9002, false, 2},
		{{    "l", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}, {"im", 16,  0xFFFF}}, 0xA008,  true, 2},
		{{    "l", {  "er",  8, 0x000E}, {  "er", -1,  0x000C}, {"im",  0, -0x003F}}, 0xB000, false, 2},
		{{    "l", {  "er",  8, 0x000E}, {  "er", -1,  0x000E}, {"im",  0, -0x003F}}, 0xB040, false, 2},
		{{    "l", {  "er",  8, 0x000E}, {  "im", 16,  0xFFFF}                     }, 0x9012,  true, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "ea",  0,  0x0000}                     }, 0x9030, false, 2},
		{{    "l", {   "r",  8, 0x000F}, { "eap",  0,  0x0000}                     }, 0x9050, false, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "er",  4,  0x000E}                     }, 0x9000, false, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "er",  4,  0x000E}, {"im", 16,  0xFFFF}}, 0x9008,  true, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "er", -1,  0x000C}, {"im",  0, -0x003F}}, 0xD000, false, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "er", -1,  0x000E}, {"im",  0, -0x003F}}, 0xD040, false, 2},
		{{    "l", {   "r",  8, 0x000F}, {  "im", 16,  0xFFFF}                     }, 0x9010,  true, 2},
		{{    "l", {  "xr",  8, 0x000C}, {  "ea",  0,  0x0000}                     }, 0x9034, false, 2},
		{{    "l", {  "xr",  8, 0x000C}, { "eap",  0,  0x0000}                     }, 0x9054, false, 2},
		{{    "l", {  "qr",  8, 0x0008}, {  "ea",  0,  0x0000}                     }, 0x9036, false, 2},
		{{    "l", {  "qr",  8, 0x0008}, { "eap",  0,  0x0000}                     }, 0x9056, false, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "ea",  0,  0x0000}                     }, 0x9033, false, 2},
		{{   "st", {  "er",  8, 0x000E}, { "eap",  0,  0x0000}                     }, 0x9053, false, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}                     }, 0x9003, false, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "er",  4,  0x000E}, {"im", 16,  0xFFFF}}, 0xA009,  true, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "er", -1,  0x000C}, {"im",  0, -0x003F}}, 0xB080, false, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "er", -1,  0x000E}, {"im",  0, -0x003F}}, 0xB0C0, false, 2},
		{{   "st", {  "er",  8, 0x000E}, {  "im", 16,  0xFFFF}                     }, 0x9013,  true, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "ea",  0,  0x0000}                     }, 0x9031, false, 2},
		{{   "st", {   "r",  8, 0x000F}, { "eap",  0,  0x0000}                     }, 0x9051, false, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "er",  4,  0x000E}                     }, 0x9001, false, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "er",  4,  0x000E}, {"im", 16,  0xFFFF}}, 0x9009,  true, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "er", -1,  0x000C}, {"im",  0, -0x003F}}, 0xD080, false, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "er", -1,  0x000E}, {"im",  0, -0x003F}}, 0xD0C0, false, 2},
		{{   "st", {   "r",  8, 0x000F}, {  "im", 16,  0xFFFF}                     }, 0x9011,  true, 2},
		{{   "st", {  "xr",  8, 0x000C}, {  "ea",  0,  0x0000}                     }, 0x9035, false, 2},
		{{   "st", {  "xr",  8, 0x000C}, { "eap",  0,  0x0000}                     }, 0x9055, false, 2},
		{{   "st", {  "qr",  8, 0x0008}, {  "ea",  0,  0x0000}                     }, 0x9037, false, 2},
		{{   "st", {  "qr",  8, 0x0008}, { "eap",  0,  0x0000}                     }, 0x9057, false, 2},
		{{  "add", {  "sp",  0, 0x0000}, {  "im",  0, -0x00FF}                     }, 0xE100, false, 0},
		{{  "mov", {"ecsr",  0, 0x0000}, {   "r",  4,  0x000F}                     }, 0xA00F, false, 0},
		{{  "mov", { "elr",  0, 0x0000}, {  "er",  8,  0x000E}                     }, 0xA00D, false, 0},
		{{  "mov", {"epsw",  0, 0x0000}, {   "r",  4,  0x000F}                     }, 0xA00C, false, 0},
		{{  "mov", {  "er",  8, 0x000E}, { "elr",  0,  0x0000}                     }, 0xA005, false, 0},
		{{  "mov", {  "er",  8, 0x000E}, {  "sp",  0,  0x0000}                     }, 0xA01A, false, 0},
		{{  "mov", { "psw",  0, 0x0000}, {   "r",  4,  0x000F}                     }, 0xA00B, false, 0},
		{{  "mov", { "psw",  0, 0x0000}, {  "im",  0,  0x00FF}                     }, 0xE900, false, 0},
		{{  "mov", {   "r",  8, 0x000F}, {"ecsr",  0,  0x0000}                     }, 0xA007, false, 0},
		{{  "mov", {   "r",  8, 0x000F}, {"epsw",  0,  0x0000}                     }, 0xA004, false, 0},
		{{  "mov", {   "r",  8, 0x000F}, { "psw",  0,  0x0000}                     }, 0xA003, false, 0},
		{{  "mov", {  "sp",  0, 0x0000}, {  "er",  4,  0x000E}                     }, 0xA10A, false, 0},
		{{ "push", {  "er",  8, 0x000E}                                            }, 0xF05E, false, 0},
		{{ "push", {  "qr",  8, 0x0008}                                            }, 0xF07E, false, 0},
		{{ "push", {   "r",  8, 0x000F}                                            }, 0xF04E, false, 0},
		{{ "push", {  "xr",  8, 0x000C}                                            }, 0xF06E, false, 0},
		{{ "push", {"push",  8, 0x000F}                                            }, 0xF0CE, false, 0},
		{{  "pop", {  "er",  8, 0x000E}                                            }, 0xF01E, false, 0},
		{{  "pop", {  "qr",  8, 0x0008}                                            }, 0xF03E, false, 0},
		{{  "pop", {   "r",  8, 0x000F}                                            }, 0xF00E, false, 0},
		{{  "pop", {  "xr",  8, 0x000C}                                            }, 0xF02E, false, 0},
		{{  "pop", { "pop",  8, 0x000F}                                            }, 0xF08E, false, 0},
		{{  "mov", {  "cr",  8, 0x000F}, {   "r",  4,  0x000F}                     }, 0xA00E, false, 0},
		{{  "mov", { "cer",  8, 0x000E}, {  "ea",  0,  0x0000}                     }, 0xF02D, false, 2},
		{{  "mov", { "cer",  8, 0x000E}, { "eap",  0,  0x0000}                     }, 0xF03D, false, 2},
		{{  "mov", {  "cr",  8, 0x000F}, {  "ea",  0,  0x0000}                     }, 0xF00D, false, 2},
		{{  "mov", {  "cr",  8, 0x000F}, { "eap",  0,  0x0000}                     }, 0xF01D, false, 2},
		{{  "mov", { "cxr",  8, 0x000C}, {  "ea",  0,  0x0000}                     }, 0xF04D, false, 2},
		{{  "mov", { "cxr",  8, 0x000C}, { "eap",  0,  0x0000}                     }, 0xF05D, false, 2},
		{{  "mov", { "cqr",  8, 0x0008}, {  "ea",  0,  0x0000}                     }, 0xF06D, false, 2},
		{{  "mov", { "cqr",  8, 0x0008}, { "eap",  0,  0x0000}                     }, 0xF07D, false, 2},
		{{  "mov", {   "r",  4, 0x000F}, {  "cr",  8,  0x000F}                     }, 0xA006, false, 0},
		{{  "mov", {  "ea",  0, 0x0000}, { "cer",  8,  0x000E}                     }, 0xF0AD, false, 1},
		{{  "mov", { "eap",  0, 0x0000}, { "cer",  8,  0x000E}                     }, 0xF0BD, false, 1},
		{{  "mov", {  "ea",  0, 0x0000}, {  "cr",  8,  0x000F}                     }, 0xF08D, false, 1},
		{{  "mov", { "eap",  0, 0x0000}, {  "cr",  8,  0x000F}                     }, 0xF09D, false, 1},
		{{  "mov", {  "ea",  0, 0x0000}, { "cxr",  8,  0x000C}                     }, 0xF0CD, false, 1},
		{{  "mov", { "eap",  0, 0x0000}, { "cxr",  8,  0x000C}                     }, 0xF0DD, false, 1},
		{{  "mov", {  "ea",  0, 0x0000}, { "cqr",  8,  0x0008}                     }, 0xF0ED, false, 1},
		{{  "mov", { "eap",  0, 0x0000}, { "cqr",  8,  0x0008}                     }, 0xF0FD, false, 1},
		{{  "lea", {  "er",  4, 0x000E}                                            }, 0xF00A, false, 1},
		{{  "lea", {  "er",  4, 0x000E}, {  "im", 16,  0xFFFF}                     }, 0xF00B,  true, 1},
		{{  "lea", {  "im", 16, 0xFFFF}                                            }, 0xF00C,  true, 1},
		{{  "daa", {   "r",  8, 0x000F}                                            }, 0x801F, false, 0},
		{{  "das", {   "r",  8, 0x000F}                                            }, 0x803F, false, 0},
		{{  "neg", {   "r",  8, 0x000F}                                            }, 0x805F, false, 0},
		{{   "sb", {   "r",  8, 0x000F}, {  "bo",  4,  0x0007}                     }, 0xA000, false, 0},
		{{   "sb", {  "im", 16, 0xFFFF}, {  "bo",  4,  0x0007}                     }, 0xA080,  true, 1},
		{{   "rb", {   "r",  8, 0x000F}, {  "bo",  4,  0x0007}                     }, 0xA002, false, 0},
		{{   "rb", {  "im", 16, 0xFFFF}, {  "bo",  4,  0x0007}                     }, 0xA082,  true, 1},
		{{   "tb", {   "r",  8, 0x000F}, {  "bo",  4,  0x0007}                     }, 0xA001, false, 0},
		{{   "tb", {  "im", 16, 0xFFFF}, {  "bo",  4,  0x0007}                     }, 0xA081,  true, 1},
		{{   "ei",                                                                 }, 0xED08, false, 0},
		{{   "di",                                                                 }, 0xEBF7, false, 0},
		{{   "sc",                                                                 }, 0xED80, false, 0},
		{{   "rc",                                                                 }, 0xEB7F, false, 0},
		{{ "clpc",                                                                 }, 0xFECF, false, 0},
		{{   "bc", {  "co",  8, 0x000F}, {  "jo",  0, -0x00FF}                     }, 0xC000, false, 0},
		{{"extbw", {  "er", -1, 0x0000}                                            }, 0x810F, false, 0},
		{{"extbw", {  "er", -1, 0x0002}                                            }, 0x832F, false, 0},
		{{"extbw", {  "er", -1, 0x0004}                                            }, 0x854F, false, 0},
		{{"extbw", {  "er", -1, 0x0006}                                            }, 0x876F, false, 0},
		{{"extbw", {  "er", -1, 0x0008}                                            }, 0x898F, false, 0},
		{{"extbw", {  "er", -1, 0x000A}                                            }, 0x8BAF, false, 0},
		{{"extbw", {  "er", -1, 0x000C}                                            }, 0x8DCF, false, 0},
		{{"extbw", {  "er", -1, 0x000E}                                            }, 0x8FEF, false, 0},
		{{  "swi", {  "im",  0, 0x003F}                                            }, 0xE500, false, 0},
		{{  "brk",                                                                 }, 0xFFFF, false, 0},
		{{    "b", {  "im",  8, 0x000F}, {  "im", 16,  0xFFFF}                     }, 0xF000,  true, 0},
		{{    "b", {  "er",  4, 0x000E}                                            }, 0xF002, false, 0},
		{{   "bl", {  "im",  8, 0x000F}, {  "im", 16,  0xFFFF}                     }, 0xF001,  true, 0},
		{{   "bl", {  "er",  4, 0x000E}                                            }, 0xF003, false, 0},
		{{  "mul", {  "er",  8, 0x000E}, {   "r",  4,  0x000F}                     }, 0xF004, false, 0},
		{{  "div", {  "er",  8, 0x000E}, {   "r",  4,  0x000F}                     }, 0xF009, false, 0},
		{{  "inc", {  "ea",  0, 0x0000}                                            }, 0xFE2F, false, 1},
		{{  "dec", {  "ea",  0, 0x0000}                                            }, 0xFE3F, false, 1},
		{{   "rt",                                                                 }, 0xFE1F, false, 0},
		{{  "rti",                                                                 }, 0xFE0F, false, 0},
		{{  "nop",                                                                 }, 0xFE8F, false, 0},
		{{  "dsr", { "dsr", -1, 0xFFFF}                                            }, 0xFE9F, false, 0},
		{{  "dsr", {   "r",  4, 0x000F}                                            }, 0x900F, false, 0},
		{{  "dsr", {  "im",  0, 0x00FF}                                            }, 0xE300, false, 0},
	}

	for ix, def in next, instruction_source do
		local repr, imask, has_imm, addr = def[1], def[2], def[3], def[4]
		local function instruction_emit_func(instruction, imm16)
			local params_out = {}
			for ix = 2, #repr do
				local fmt, shift, pmask = unpack(repr[ix])
				local signed = pmask < 0
				pmask = math.abs(pmask)
				local fmtvalue = instruction
				if shift == 16 then
					shift = 0
					fmtvalue = imm16
				end
				if shift < 0 then
					shift = 0
					fmtvalue = pmask
					pmask = -1
				end
				fmtvalue = bit.band(bit.rshift(fmtvalue, shift), pmask)
				if signed then
					if bit.band(fmtvalue, bit.rshift(pmask + 1, 1)) ~= 0 then
						fmtvalue = bit.bor(bit.bxor(-1, pmask), fmtvalue)
					end
				end
				table.insert(params_out, {fmtvalue, fmt})
			end
			local result = {
				mnemonic = repr[1],
				instruction = instruction,
				imm16 = imm16,
				params = params_out,
				offsetable = addr
			}
			return result
		end
		local vmask = 0
		for ix = 2, #repr do
			local shift, pmask = repr[ix][2], math.abs(repr[ix][3])
			if shift >= 0 then
				vmask = bit.bor(vmask, bit.lshift(pmask, shift))
			end
		end
		local maskvariants = {[0] = true}
		for ix = 0, 15 do
			local cbit = bit.lshift(1, ix)
			if bit.band(vmask, cbit) ~= 0 then
				local newmaskvariants = {}
				for key in next, maskvariants do
					newmaskvariants[key] = true
					newmaskvariants[bit.bor(key, cbit)] = true
				end
				maskvariants = newmaskvariants
			end
		end
		local instruction_stub = {
			has_imm,
			instruction_emit_func
		}
		for key in next, maskvariants do
			local lkey = bit.bor(imask, key) + 1
			if instruction_lookup[lkey] then
				printf("%04X, %04X, %04X", imask, key, lkey - 1)
				panic("lookupgen: clash")
			end
			instruction_lookup[lkey] = instruction_stub
		end
	end
	do
		local instruction_stub = {
			false,
			function(instruction, imm16)
				return {
					mnemonic = "?",
					instruction = instruction,
					imm16 = imm16,
					params = {},
					offsetable = 0,
					opcode = {instruction}
				}
			end
		}
		for ix = 0x0000, 0xFFFF do
			if not instruction_lookup[ix + 1] then
				instruction_lookup[ix + 1] = instruction_stub
			end
		end
	end
end
print("  Done.")

local function fetch(address)
	return binary_source:byte(address + 1) + 0x100 * binary_source:byte(address + 2)
end

--instruction_by_address: {full_address -> union[instruction_type, tail_type]}
--	where
--	instruction_type = {
--		address: number,  -- full_address
--		dsr: param_type,
--		length: number,
--
--		mnemonic: string,
--		params: {param_type...}
--		offsetable: number = 0 | index to 'params'
--
--		opcode: {number...},  -- each number is 16 bit
--		instruction,  -- unused, == opcode[1]
--		imm16,  -- unused
--
--		context: context_type,
--		under_label: label_type,
--		break_streak: true|nil,
--	}
--	param_type = {{number, type: str = key of table 'formats'}
--	tail_type = { head: instruction_type }
--	context_type = {
--		name: string,
--		head: label_type,
--	}
--	label_type = {
--		# see function add_label
--	}
local instruction_by_address = {}
local function disassemble(segment, address)
	local full_address = segment + address
	local fetch_addr = address
	if instruction_by_address[full_address] then
		if instruction_by_address[full_address].head then
			panic("disassemble: requesting tail at %01X:%04X", bit.rshift(segment, 16), address)
		end
		return instruction_by_address[full_address], true
	end
	local instruction_data = {}

	local state = {}
	local instruction_stub

	local length = 0
	local opcode, imm16, dsr, result
	local opcode_data = {}
	while true do
		local word = fetch(segment + fetch_addr)
		table.insert(opcode_data, word)
		fetch_addr = bit.band(fetch_addr + 2, 0xFFFF)
		length = length + 2
		if imm16 == true then
			imm16 = word
		end
		if not opcode then
			opcode = word
			instruction_stub = instruction_lookup[opcode + 1]
			if instruction_stub[1] then
				imm16 = true
			end
		end
		if imm16 ~= true then
			result = instruction_stub[2](opcode, imm16)
		end
		if result then
			if result.mnemonic == "dsr" then
				if dsr then
					panic("disassemble: double dsr at %01X:%04X", bit.rshift(segment, 16), address)
				end
				dsr = result.params[1]
				opcode = false
				imm16 = false
				result = false
			elseif result.mnemonic == "push" and result.params[1][2] == "push" then
				local mask = result.params[1][1]
				result.params = {}
				if bit.band(mask, 2) ~= 0 then table.insert(result.params, {0,  "elr"}) end
				if bit.band(mask, 4) ~= 0 then table.insert(result.params, {0, "epsw"}) end
				if bit.band(mask, 8) ~= 0 then table.insert(result.params, {0,   "lr"}) end
				if bit.band(mask, 1) ~= 0 then table.insert(result.params, {0,   "ea"}) end
			elseif result.mnemonic == "pop" and result.params[1][2] == "pop" then
				local mask = result.params[1][1]
				result.params = {}
				if bit.band(mask, 1) ~= 0 then table.insert(result.params, {0,   "ea"}) end
				if bit.band(mask, 8) ~= 0 then table.insert(result.params, {0,   "lr"}) end
				if bit.band(mask, 4) ~= 0 then table.insert(result.params, {0,  "psw"}) end
				if bit.band(mask, 2) ~= 0 then table.insert(result.params, {0,   "pc"}) end
			end
		end
		if result then
			break
		end
	end
	result.address = full_address
	result.dsr = dsr
	result.length = length
	result.opcode = opcode_data
	instruction_by_address[full_address] = result
	for ix = 2, length - 2 do
		instruction_by_address[full_address + ix] = {
			head = result
		}
	end
	return result
end

local datalabel_by_address = {}
local function add_data_label(address, xref)
	local label_obj = datalabel_by_address[address]
	if not label_obj then
		label_obj = {
			address = address,
			name = DATA_LABEL_FORMAT:format(address),
			xrefs = {}
		}
		datalabel_by_address[address] = label_obj
	end
	if xref then
		label_obj.xrefs[xref] = true
	end
	return label_obj
end

local label_by_address = {}
local function add_label(streak, address, xref)
	local label_obj = label_by_address[address]
	if not label_obj then
		label_obj = {
			address = address,
			name = GLOBAL_LABEL_FORMAT:format(address),
			xrefs = {},
			streak = streak
		}
		label_by_address[address] = label_obj
	end
	if xref then
		label_obj.xrefs[xref] = true
	end
	return label_obj
end

--comments_by_address: {full_address -> union[table[type='jt'], str]}
local comments_by_address = {}
local function add_comment(address, comment)
	comments_by_address[address] = comments_by_address[address] or {}
	table.insert(comments_by_address[address], comment)
end
local function add_jt_comment(address, ix, label_obj)
	add_comment(address, {
		type = "jt",
		ix = ix,
		label = label_obj
	})
end

local function make_streak()
	return {
		friends = {}
	}
end

local image_iterator
do
	local function image_next(st, address)
		if address >= st.length then
			return
		end
		local instr = st.store[address]
		local next_address
		if instr then
			next_address = address + instr.length
		else
			next_address = address + 2
		end
		return next_address, address, instr, st.label[address]
	end

	function image_iterator()
		return image_next, {
			length = binary_source_length,
			store = instruction_by_address,
			label = label_by_address
		}, 0
	end
end

print("Disassembling binary...")

local to_disassemble = {}

function add_disassemble_address(address)
	local new_streak = make_streak()
	local label_obj = add_label(new_streak, address)
	to_disassemble[{bit.band(address, 0xF0000), bit.band(address, 0xFFFF),
			new_streak}] = true
end

do
	local entry_ords_in = {}
	for entry in args_assoc.entry:gmatch("[^,]+") do
		local entry_ord = tonumber(entry)
		if not entry_ord then
			panic("invalid entry ordinal %s", entry)
		end
		entry_ords_in[entry_ord] = true
	end
	if args_assoc.complement_entries then
		for entry_ord = 1, 127 do
			entry_ords_in[entry_ord] = not entry_ords_in[entry_ord] and true or nil
		end
	end
	for entry_ord in next, entry_ords_in do
		local address = fetch(math.floor(2 * entry_ord))
		if address % 2 == 0 then
			add_disassemble_address(address)
		else
			printf("ignoring entry %s (%04X)", entry_ord, address)
		end
	end
end

local rename_list
if args_assoc.names then
	print("Reading rename list...")
	local handle = io.open(args_assoc.names, "r")
	if not handle then
		panic("Failed to open \"%s\"", args_assoc.names)
	end
	local name_content = handle:read("*a")
	handle:close()

	rename_list = {}
	for line in name_content:gmatch("[^\n]+") do
		local raw, real = line:match("^%s*([%w_%.]+)%s+([%w_%.]+)")
		if not raw then
			raw = line:match("^%s*([%w_%.]+)")
		end

		if raw then
			local addr = tonumber(raw, 16)
			if addr then
				add_disassemble_address(addr)
				raw = addr
			end
			if real then
				rename_list[#rename_list+1] = {raw, real}
			end
		end
	end
	print("  Done.")
end

local resolve_variable_branch
do
	local function prev_instr(instr)
		local result = instruction_by_address[bit.band(instr.address, 0xF0000) + bit.band(instr.address - 2, 0xFFFF)]
		if not result then
			return
		end
		return result.head or result
	end

	local function instruction_match(instr, mnemonic, ...)
		if not instr then
			return
		end
		if instr.mnemonic ~= mnemonic then
			return
		end
		return true
	end

	function resolve_variable_branch(bl_instr, new_to_disassemble, streak)
		--[[
		cmp A, B
		cmpc A, B
		bc A
		b A
		(
		cmp A, B
		cmpc A, B
		bc A
		b A
		)?
		(
		add A, B
		addc A, B
		)?
		sllc A
		sll A
		l A, (B:)?C[D]
		]]

		local l_instr = prev_instr(bl_instr)
		if not instruction_match(l_instr, "l") then
			return
		end
		if l_instr.dsr then
			return
		end
		if not l_instr.params[3] or l_instr.params[3][2] ~= "dlab" then
			return
		end
		local map_base = l_instr.params[3][1].address

		local sll_instr = prev_instr(l_instr)
		if not instruction_match(sll_instr, "sll") then
			return
		end
		local sllc_instr = prev_instr(sll_instr)
		if not instruction_match(sllc_instr, "sllc") then
			return
		end
		local offset_instr = prev_instr(sllc_instr)

		local offset = 0

		local offset_instr_2 = prev_instr(offset_instr)
		if instruction_match(offset_instr, "addc") then
			offset = offset + bit.lshift(offset_instr.params[2][1], 8)
		else
			offset_instr_2 = offset_instr
		end

		local branch_4 = prev_instr(offset_instr_2)
		if instruction_match(offset_instr_2, "add") then
			offset = offset + offset_instr_2.params[2][1]
		else
			branch_4 = offset_instr_2
		end

		local branch_3, branch_1, cmp_1, cmpc_1, cmp_2, cmpc_2
		repeat
			branch_3 = prev_instr(branch_4)
			if not instruction_match(branch_4, "b") then
				branch_3 = branch_4
			end
			if not instruction_match(branch_3, "bc") then
				return
			end
			cmpc_2 = prev_instr(branch_3)

			if not instruction_match(cmpc_2, "cmpc") then
				return
			end
			cmp_2 = prev_instr(cmpc_2)
			if not instruction_match(cmp_2, "cmp") then
				return
			end
			local branch_2 = prev_instr(cmp_2)

			branch_1 = prev_instr(branch_2)
			if not instruction_match(branch_2, "b") then
				branch_1 = branch_2
			end
			if not instruction_match(branch_1, "bc") then
				break
			end
			if branch_1.params[1][1] > 3 then
				break
			end
			cmpc_1 = prev_instr(branch_1)

			if not instruction_match(cmpc_1, "cmpc") then
				return
			end
			cmp_1 = prev_instr(cmpc_1)
			if not instruction_match(cmp_1, "cmp") then
				return
			end
		until true

		local cmp_value, cmp_cond = {}, {}
		if cmp_1 then
			cmp_value[1] = cmp_1.params[2][1] + bit.lshift(cmpc_1.params[2][1], 8)
			cmp_cond[1] = branch_1.params[1][1]
		end
		cmp_value[2] = cmp_2.params[2][1] + bit.lshift(cmpc_2.params[2][1], 8)
		cmp_cond[2] = branch_3.params[1][1]

		local min, max
		for ix = 1, 2 do
			if cmp_cond[ix] then
				if cmp_cond[ix] == 0 then
					min = cmp_value[ix]
				end
				if cmp_cond[ix] == 1 then
					max = cmp_value[ix] - 1
				end
				if cmp_cond[ix] == 2 then
					min = cmp_value[ix] + 1
				end
				if cmp_cond[ix] == 3 then
					max = cmp_value[ix]
				end
			end
		end

		if not max and min then
			max = min - 1
			min = 0
		end
		if not min and max then
			min = 0
		end

		if not min or not max then
			printf("fail %05X", bl_instr.address)
			return
		end
		min = min + offset
		max = max + offset

		if max < min then
			min, max = max + 1, min - 1
		end

		min, max = bit.band(min, 0xFFFF), bit.band(max, 0xFFFF)

		add_comment(bl_instr.address, "Jump table")
		local td_segment = bit.band(bl_instr.address, 0xF0000)
		for ix = min, max do
			local td_address = fetch(bit.band(map_base + ix * 2, 0xFFFF))
			new_to_disassemble[{td_segment, td_address, streak}] = true
			local label_obj = add_label(streak, td_segment + td_address, bl_instr)
			add_jt_comment(bl_instr.address, ix, label_obj)
		end

		return true
	end
end

while next(to_disassemble) do
	local variable_branches = {}
	while next(to_disassemble) do
		local new_to_disassemble = {}
		for address_tuple in next, to_disassemble do
			local segment, address, streak = unpack(address_tuple)
			if segment + address >= binary_source_length then
				panic2("runloop: out of data at %01X:%04X", bit.rshift(segment, 16), address)
			else
				while true do
					local instr, seen = disassemble(segment, address)
					if seen then
						break
					end
					if instr.mnemonic == "?" then
						panic2("runloop: unknown instruction at %01X:%04X", bit.rshift(segment, 16), address)
						break
					end
					address = bit.band(address + instr.length, 0xFFFF)
					if  instr.mnemonic == "rt"
					or  instr.mnemonic == "rti"
					or (instr.mnemonic == "pop" and (
							(instr.params[1] and instr.params[1][2] == "pc") or
							(instr.params[2] and instr.params[2][2] == "pc") or
							(instr.params[3] and instr.params[3][2] == "pc") or
							(instr.params[4] and instr.params[4][2] == "pc")
						)) then
						instr.break_streak = true
					end
					if instr.mnemonic == "bc" then
						local td_segment = segment
						local td_address = bit.band(address + instr.params[2][1] * 2, 0xFFFF)
						local label_obj = add_label(streak, td_segment + td_address, instr)
						new_to_disassemble[{td_segment, td_address, streak}] = true
						instr.params[2] = {label_obj, "lab"}
						if instr.params[1][1] == 0x000E then
							instr.break_streak = true
						end
					end
					if instr.mnemonic == "b" or instr.mnemonic == "bl" then
						if instr.params[1][2] == "im" then
							local td_segment = bit.lshift(instr.params[1][1], 16)
							local td_address = instr.params[2][1]
							local new_streak = make_streak()
							local label_obj = add_label(new_streak, td_segment + td_address, instr)
							new_to_disassemble[{td_segment, td_address, new_streak}] = true
							instr.params[2] = nil
							instr.params[1] = {label_obj, "lab"}
						else
							variable_branches[instr] = streak
						end
						if instr.mnemonic == "b" then
							instr.break_streak = true
						end
					end
					if (instr.mnemonic == "l"
					or instr.mnemonic == "st"
					or instr.mnemonic == "lea"
					or instr.mnemonic == "sb"
					or instr.mnemonic == "tb"
					or instr.mnemonic == "rb")
					and (not instr.dsr or (instr.dsr and instr.dsr[2] == "im")) then
						local td_segment = instr.dsr and instr.dsr[1] or 0
						for ix = 1, #instr.params do
							if instr.params[ix][2] == "im" and instr.params[ix][1] >= 0x100 and instr.params[ix][1] < 0xFF00 then
								local td_address = instr.params[ix][1]
								local label_obj = add_data_label(td_segment + td_address, instr)
								instr.params[ix] = {label_obj, "dlab"}
							end
						end
					end
					if instr.break_streak then
						break
					end
				end
			end
		end
		to_disassemble = new_to_disassemble
	end

	for instr, streak in next, variable_branches do
		local segment = bit.band(instr.address, 0xF0000)
		local address = bit.band(instr.address,  0xFFFF)
		if not resolve_variable_branch(instr, to_disassemble, streak) then
			printf("runloop: failed to resolve variable branch at %01X:%04X", bit.rshift(segment, 16), address)
			add_comment(instr.address, "Failed to resolve variable branch")
		end
	end
end
print("  Done.")

print("Discovering contexts...")
do
	local function make_streak_friends(one, other)
		if other.streak ~= one.streak then
			one.streak.friends[other.streak] = true
			other.streak.friends[one.streak] = true
		end
	end

	local streaks = {}
	local last_label
	for next_address, address, instr, label_obj in image_iterator() do
		if instr and label_obj then
			if last_label then
				make_streak_friends(last_label, label_obj)
			end
			last_label = label_obj
			streaks[last_label.streak] = true
		end
		if instr then
			if instr.mnemonic == "bc" or (instr.mnemonic == "b" and instr.params[1][2] == "lab") then
				local other_label_obj = instr.params[2] and instr.params[2][1] or instr.params[1][1]
				make_streak_friends(other_label_obj, last_label)
			end
			if instr.break_streak then
				last_label = nil
			end
		end
	end
	for streak in next, streaks do
		if not streak.seen then
			local friend_streaks = {}
			local streaks_to_check = {[streak] = true}
			streak.seen = true
			while next(streaks_to_check) do
				local new_streaks_to_check = {}
				for next_streak in next, streaks_to_check do
					friend_streaks[next_streak] = true
					for friend_streak in next, next_streak.friends do
						if not friend_streak.seen then
							new_streaks_to_check[friend_streak] = true
							friend_streak.seen = true
						end
					end
				end
				streaks_to_check = new_streaks_to_check
			end
			local new_context = {}
			for streak in next, friend_streaks do
				streak.context = new_context
			end
		end
	end
end
for next_address, address, instr, label in image_iterator() do
	if instr and label then
		label.context = label.streak.context
		label.streak = nil
	end
end
do
	local contexts_seen = {}
	local rewrite_from, rewrite_to
	local last_label
	for next_address, address, instr, label in image_iterator() do
		if instr and label then
			if last_label and label.context ~= last_label.context and contexts_seen[label.context] then
				if label.context ~= rewrite_from then
					rewrite_from, rewrite_to = label.context, {}
				end
				label.context = rewrite_to
			else
				rewrite_from = nil
			end
			contexts_seen[label.context] = true
			last_label = label
		end
	end
end
do
	local last_label
	for next_address, address, instr, label in image_iterator() do
		if instr and label then
			last_label = label
			if label.context.name then
				label.name = LOCAL_LABEL_FORMAT:format(label.address - label.context.head.address)
			else
				label.context.name = label.name
				label.context.head = label
				label.context_head = true
			end
		end
		if instr then
			instr.context = last_label.context
			instr.under_label = last_label
		end
	end
	for address, datalabel in next, datalabel_by_address do
		if address >= 0x10000 or address < args_assoc.rom_window then
			local instr = instruction_by_address[address]
			instr = instr and instr.head or instr
			if instr then
				datalabel.ref_instr = instr
				printf("context: warning: data label pointing into code at %01X:%04X", bit.rshift(address, 16), bit.band(address, 0xFFFF))
			end
		end
	end
end
print("  Done.")

if args_assoc.word_commands then
	print("Adding comments for word-manipulation commands...")
	local next_opcode = {
		['mov'] = 'mov',
		['cmp'] = 'cmpc',
		['add'] = 'addc',
		['sub'] = 'subc',
	}
	for addr, instr in pairs(instruction_by_address) do
		local prev_instr = instruction_by_address[addr - 2]
		if prev_instr and not prev_instr.head and not instr.head and
			next_opcode[prev_instr.mnemonic] == instr.mnemonic and
			prev_instr.context == instr.context and
			prev_instr.params[1][2] == 'r' and instr.params[1][2] == 'r' and
			prev_instr.params[2][2] == 'im' and instr.params[2][2] == 'im' then
			local r = prev_instr.params[1][1]
			if r % 2 == 0 and instr.params[1][1] == r + 1 then
				local val = instr.params[2][1] * 256 + prev_instr.params[2][1]
				local val_repr = ('%d | %04X'):format(val, val)
				if val >= 0x8000 then
					val_repr = val_repr .. (' | -%d'):format(0x10000-val)
				end
				-- try to parse a string (only ASCII supported)
				local len = 0
				local all_ascii = true
				local str = ""
				while val + len < binary_source_length do
					local char = binary_source:byte(val + len + 1)
					if char == 0 then
						break
					end
					if 0x20 > char or char > 0x7e then
						all_ascii = false
						break
					end
					str = str .. string.char(char)
					len = len + 1
					if len > 16 then
						break
					end
				end
				if all_ascii and len <= 16 and len > 0 then
					val_repr = val_repr .. ' | "' .. str:gsub('"', '\\"') .. '"'
				end

				add_comment(addr - 2, ('Equiv: %s er%d, %s'):format(
					prev_instr.mnemonic, r, val_repr))
			end
		end
	end
	print("  Done.")
end

if rename_list then
	print("Renaming labels...")
	local raw_to_real = {}
	local last_global_label
	for ix = 1, #rename_list do
		local raw, real = rename_list[ix][1], rename_list[ix][2]
		if type(raw) == 'number' then
			local addr = raw
			if not label_by_address[addr] then
				panic("rename: address %06X is not a label", addr)
			end
			raw = label_by_address[addr].name
			if label_by_address[addr].context_head then
				last_global_label = raw
			else
				raw = label_by_address[addr].context.name .. raw
			end
		end
		if raw:find("^%.") then
			-- .l_123
			if not last_global_label then
				panic("rename: fix that rename list pls")
			end
			raw = last_global_label .. raw
		elseif raw:find("%.") then
			-- f_12345.l_123
		else
			-- f_12345
			last_global_label = raw
		end
		if raw_to_real[raw] then
			panic("rename: duplicate entry %s -> %s", raw, real)
		end
		raw_to_real[raw] = real
	end

	local raw_used = {}
	for address, label in next, label_by_address do
		local raw = label.name:find("^%.") and (label.context.name .. label.name) or label.name
		local real = raw_to_real[raw]
		if real then
			raw_used[raw] = true
			label.name = real
		end
	end
	for address, label in next, label_by_address do
		if label.context then
			local raw = label.context.name
			local real = raw_to_real[raw]
			if real then
				raw_used[raw] = true
				label.context.name = real
			end
		end
	end
	for address, datalabel in next, datalabel_by_address do
		local raw = datalabel.name
		local real = raw_to_real[raw]
		if real then
			raw_used[raw] = true
			datalabel.name = real
		end
	end
	for raw, real in pairs(raw_to_real) do
		if not raw_used[raw] then
			printf("rename: unused label %q -> %q", raw, real)
		end
	end
	print("  Done.")
end

print("Writing disassembly...")
local handle = io.open(args_assoc.output, "w")
if not handle then
	panic("Failed to open \"%s\"", args_assoc.output)
end
do
	local last_instr
	for next_address, address, instr, label in image_iterator() do
		local instr = instruction_by_address[address]
		if ((not last_instr) ~= (not instr)) or (last_instr and instr and last_instr.context ~= instr.context) then
			handle:write("\n\n\n")
		end

		local comments = comments_by_address[address]
		if comments then
			for ix = 1, #comments do
				handle:write("; ")
				if comments[ix].type == "jt" then
					handle:write(("  %4i | %s"):format(comments[ix].ix, formats.lab:format(comments[ix].label, instr)))
				else
					handle:write(comments[ix])
				end
				handle:write("\n")
			end
		end
		if label then
			handle:write(("%s:\n"):format(label.name))
		end

		local out_head
		local out_body = {}
		local out_tail
		if args_assoc.addresses then
			out_tail = ("; %05X |"):format(address)
		end

		if instr then
			out_head = instr.mnemonic
			for ix = 1, #instr.params do
				if instr.offsetable ~= 0 and instr.offsetable == ix - 1 and (instr.params[ix][2] == "dlab" or instr.params[ix][2] == "im") then
					if instr.params[ix][2] == "im" and instr.params[ix][1] >= 0xFF00 then
						instr.params[ix][1] = instr.params[ix][1] - 0x10000
					end
					out_body[#out_body] = out_body[#out_body]:gsub("%[", formats[instr.params[ix][2]]:format(instr.params[ix][1], instr) .. "[")
				else
					local param = ''
					if instr.offsetable == ix then
						if instr.dsr then
							param = param .. formats[instr.dsr[2]]:format(instr.dsr[1], instr)
							param = param .. ":"
						end
						param = param .. "["
					end
					param = param .. formats[instr.params[ix][2]]:format(instr.params[ix][1], instr)
					if instr.offsetable == ix then
						param = param .. "]"
					end
					table.insert(out_body, param)
				end
			end
			if args_assoc.addresses then
				for ix = 1, instr.length / 2 do
					out_tail = out_tail .. ' ' .. ("%04X"):format(instr.opcode[ix])
				end
			end
		else
			if address >= 0x10000 or address < args_assoc.rom_window then
				local datalabel = datalabel_by_address[address]
				if datalabel then
					handle:write(("%s:\n"):format(datalabel.name))
				end
			end
			out_head = "dw"
			local data = fetch(address)
			table.insert(out_body, ("0x%04X"):format(data))
			if args_assoc.addresses then
				out_tail = out_tail .. ' ' .. ("%04X"):format(data)
			end
		end

		out_head = out_head .. ' ' .. table.concat(out_body, ", ")

		if args_assoc.addresses then
			handle:write(("\t%-30s "):format(out_head))
			handle:write(out_tail .. "\n")
		else
			handle:write(("\t%s\n"):format(out_head))
		end

		last_instr = instr
	end
end
handle:close()
print("  Done.")




