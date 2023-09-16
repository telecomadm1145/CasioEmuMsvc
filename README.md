NOTE: This is a **modified** version of the Casio emulator developed by [LBPHacker](../../../../LBPHacker) and [user202729](../../../../user202729)   
This repository will get updated with new models in the `models` folder. Note that ROMs are **not** included (for copyright reasons), you have to obtain one from somewhere else or dump it from a real calculator or emulator. (note that models labeled with `_emu` are for ROMs dumped from official emulators)


# CasioEmuX

An emulator and disassembler for the CASIO calculator series using the nX-U8/100 core.  
With debuggers.

## Build
0. install xmake  
   `curl -fsSL https://xmake.io/shget.text | bash` 
1. notice that this project has already included the full `fx991cn` debugging resources  
   which means you don need to download any extra resources for `fx991cnx`  
   To run debug on it, please:  
   ```
   cd emulator
   xmake
   xmake run CasioEmuX ../models/fx991cnx
   ```  
   Other models please follow the instructions below:

2. build `disas`  
   ```
   cd disas
   make
   ```
3. build model directory  
	download `rom` as name `rom.bin`  in your model's directory
   
   for example, fx991cnx:
   ```
	cd disas
   ```
   REMEMBER cd to `disas` IS IMPORTANT!  
   ```
   ./bin/u8-disas ../models/fx991cnx/rom.bin  0 0x40000 ./_disas.txt
   ```
   check _disas.txt and make sure it successfully generated  
   then, copy it to your model's directory
   ```
   cp ./_disas.txt ../models/fx991cnx/
   ```
   then modify `model.lua`  
   set `rom_path` to `"rom.bin"`  

4. build emulator & debugger
   ```
   cd emulator
   xmake
   ``` 
   wait until everything is done  
   then  
	```
	xmake run CasioEmuX ../models/fx991cnx
	```