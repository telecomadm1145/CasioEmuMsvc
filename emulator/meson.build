project('emulator', 'cpp', version: '1.0.0')

lua_dependency = dependency('lua53', required : false)
if not lua_dependency.found()
	lua_dependency = dependency('lua5.3')
endif

executable('emulator', sources: [
	'src/Logger.cpp',
	'src/Peripheral/ROMWindow.cpp',
	'src/Peripheral/Timer.cpp',
	'src/Peripheral/Screen.cpp',
	'src/Peripheral/StandbyControl.cpp',
	'src/Peripheral/Keyboard.cpp',
	'src/Peripheral/Miscellaneous.cpp',
	'src/Peripheral/Peripheral.cpp',
	'src/Peripheral/BatteryBackedRAM.cpp',
	'src/Data/ModelInfo.cpp',
	'src/casioemu.cpp',
	'src/Emulator.cpp',
	'src/Chipset/CPUPushPop.cpp',
	'src/Chipset/CPU.cpp',
	'src/Chipset/InterruptSource.cpp',
	'src/Chipset/Chipset.cpp',
	'src/Chipset/CPUArithmetic.cpp',
	'src/Chipset/MMU.cpp',
	'src/Chipset/CPUControl.cpp',
	'src/Chipset/MMURegion.cpp',
	'src/Chipset/CPULoadStore.cpp',
], dependencies: [
	dependency('threads'),
	dependency('SDL2'),
	dependency('SDL2_image'),
	lua_dependency,
	dependency('readline'),
])
