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

    if not next(break_targets) then
        -- if break_targets is initially empty and later non-empty
		emu:post_tick(post_tick)
	end

	break_targets[addr] = commands
end

function unbreak_at(addr)
	if not addr then
		addr = get_real_pc()
	end
	break_targets[addr] = nil

    if not next(break_targets) then
        emu:post_tick(nil)
    end
end

function cont()
	emu:set_paused(false)
end

function post_tick()
	local real_pc = get_real_pc()
	local commands = break_targets[real_pc]
	if commands then
		printf("********** breakpoint reached at %05X **********", real_pc)
		emu:set_paused(true)
		commands()
	end
end

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
cpu.bt          Current stack trace.

code            Access code. (By words, only use even address,
                otherwise program will panic)
data            Access data. (By bytes)
data:watch		Set write watchpoint.
data:rwatch		Set read watchpoint.
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

