do
	local buttons = {}
	local function generate(px, py, w, h, nx, ny, sx, sy, code)
		local cp = 1
		for iy = 0, ny - 1 do
			for ix = 0, nx - 1 do
				table.insert(buttons, {px + ix*sx, py + iy*sy, w, h, code[cp], code[cp+1]})
				cp = cp + 2
			end
		end
	end
	-- Refer to https://wiki.libsdl.org/SDL_Keycode for key names.
	generate(46, 544, 58, 41, 5, 4, 65, 57, {
		0x02, '7', 0x12, '8', 0x22, '9', 0x32, 'Backspace', 0x42, 'Space',
		0x01, '4', 0x11, '5', 0x21, '6', 0x31, '' , 0x41, '/',
		0x00, '1', 0x10, '2', 0x20, '3', 0x30, '=', 0x40, '-',
		0x64, '0', 0x63, '.', 0x62, 'E', 0x61, '' , 0x60, 'Return',
	})
	generate(46, 406, 48, 31, 6, 3, 54, 46, {
		0x05, '', 0x15, '', 0x25, '', 0x35, '', 0x45, '', 0x55, '',
		0x04, '', 0x14, '', 0x24, '', 0x34, '', 0x44, '', 0x54, '',
		0x03, '', 0x13, '', 0x23, '', 0x33, '', 0x43, '', 0x53, '',
	})
	generate( 40, 359, 48, 31, 2, 1,  54,  0, {0x06, 'F5', 0x16, 'F6',})
	generate(268, 359, 48, 31, 2, 1,  54,  0, {0x46, 'F7', 0x56, 'F8',})
	generate( 44, 290, 49, 39, 2, 1, 273,  0, {0x07, 'F1', 0xFF, 'F4',})
	generate(100, 298, 48, 38, 2, 1, 162,  0, {0x17, 'F2', 0x47, 'F3',})
	generate(155, 319, 33, 32, 2, 1,  67,  0, {0x26, 'Left', 0x37, 'Right',})
	generate(188, 289, 34, 30, 1, 2,   0, 62, {0x27, 'Up', 0x36, 'Down',})

	emu:model({
		model_name = "fx-570ES PLUS",
		interface_image_path = "interface.png",
		rom_path = "rom.bin",
		csr_mask = 0x0001,
		rsd_interface = {0, 0, 410, 810, 0, 0},
		rsd_pixel = {410, 252,  3,  3,  61, 141},
		rsd_s     = {410,   0, 10, 14,  61, 127},
		rsd_a     = {410,  14, 11, 14,  70, 127},
		rsd_m     = {410,  28, 10, 14,  81, 127},
		rsd_sto   = {410,  42, 20, 14,  91, 127},
		rsd_rcl   = {410,  56, 19, 14, 110, 127},
		rsd_stat  = {410,  70, 24, 14, 130, 127},
		rsd_cmplx = {410,  84, 32, 14, 154, 127},
		rsd_mat   = {410,  98, 20, 14, 186, 127},
		rsd_vct   = {410, 112, 20, 14, 205, 127},
		rsd_d     = {410, 126, 12, 14, 225, 127},
		rsd_r     = {410, 140, 10, 14, 236, 127},
		rsd_g     = {410, 154, 11, 14, 246, 127},
		rsd_fix   = {410, 168, 17, 14, 257, 127},
		rsd_sci   = {410, 182, 16, 14, 273, 127},
		rsd_math  = {410, 196, 24, 14, 289, 127},
		rsd_down  = {410, 210, 10, 14, 313, 127},
		rsd_up    = {410, 224, 10, 14, 319, 127},
		rsd_disp  = {410, 238, 20, 14, 329, 127},
		ink_colour = {30, 52, 90},
		button_map = buttons
	})
end

local break_targets = {}

local function get_real_pc()
	return (cpu.csr << 16) | cpu.pc
end

function break_at(addr, commands)
	if not addr then
		addr = get_real_pc()
	end
	if commands then
		if type(commands) ~= 'function' then
			printf('Invalid secomd argument to break_at: %s', commands)
			return
		end
	else
		commands = function() end
	end
	break_targets[addr] = commands
end

function unbreak_at(addr)
	if not addr then
		addr = get_real_pc()
	end
	break_targets[addr] = nil
end

function cont()
	emu:set_paused(false)
end

emu:post_tick(function()
	local real_pc = get_real_pc()
	local commands = break_targets[real_pc]
	if commands then
		printf("********** breakpoint reached at %05X **********", real_pc)
		emu:set_paused(true)
		commands()
	end
end)

function printf(...)
	print(string.format(...))
end

function ins()
	printf("%02X %02X %02X %02X | %01X:%04X | %02X %01X:%04X", cpu.r0, cpu.r1, cpu.r2, cpu.r3, cpu.csr,  cpu.pc, cpu.psw, cpu.lcsr, cpu.lr)
	printf("%02X %02X %02X %02X | S %04X | %02X %01X:%04X", cpu.r4, cpu.r5, cpu.r6, cpu.r7, cpu.sp, cpu.epsw1, cpu.ecsr1, cpu.elr1)
	printf("%02X %02X %02X %02X | A %04X | %02X %01X:%04X", cpu.r8, cpu.r9, cpu.r10, cpu.r11, cpu.ea, cpu.epsw2, cpu.ecsr2, cpu.elr2)
	printf("%02X %02X %02X %02X | ELVL %01X | %02X %01X:%04X", cpu.r12, cpu.r13, cpu.r14, cpu.r15, cpu.psw & 3, cpu.epsw3, cpu.ecsr3, cpu.elr3)
end

function help()
	print([[
The supported functions are:

printf()        Print with format.
ins             Log all register values to the screen.
break_at        Set breakpoint.
                If input not specified, break at current address.
                Second argument (optional) is a function that is executed whenever
                this breakpoint hits.
unbreak_at      Delete breakpoint.
                If input not specified, delete breakpoint at current address.
                Have no effect if there is no breakpoint at specified position.
cont()          Continue program execution.
inject          Inject 100 bytes to the input field.
pr_stack()      Print 48 bytes of the stack before and after SP.

emu:set_paused  Set emulator state.
emu:tick()      Execute one command.
emu:shutdown()  Shutdown the emulator.

cpu.xxx         Get register value.

code            Access code. (By words, only use even address,
                otherwise program will panic)
data            Access data. (By bytes)
help()          Print this help message.
p(...)          Shorthand for `print`.
]])
end

function print_number(address) -- Calculator's 10-byte decimal fp number as hex.
	local x = {};
	for i = 0,9,1 do
		table.insert(x, string.format("%02x", data[address+i]));
	end;
	print(table.concat(x, ' '));
end

p = print

function inject(str)
	if 200 ~= #str then
		print "Input 200 hexadecimal digits please"
		return
	end

	adr = 0x8154
	for byte in str:gmatch '..' do
		data[adr] = tonumber(byte, 16)
		adr = adr + 1
	end
end

function pr_stack(radius)
	radius = radius or 48

	sp = cpu.sp
	w = io.write
	linecnt = 0
	for i = sp-radius, sp+radius-1 do
		if i >= 0x8e00 then
			break
		end

		w(  ('%s%02x'):format(i==sp and '*' or ' ', data[i])  )
		linecnt = linecnt+1
		if linecnt==16 then
			w('\n')
			linecnt = 0
		end
	end
	p()
end

