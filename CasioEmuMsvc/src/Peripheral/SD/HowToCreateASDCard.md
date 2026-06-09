1. Create a sdcard.vhd in "Computer Management"(`compmgmt.msc`)
2. Initialize as a MBR format disk. Create a Fat32 partition.
3.
Run this command in WSL or Windows with qemu-img installed
```sh
qemu-img convert -f vpc -O raw sdcard.vhd sdcard.img
```

4. Copy the output `sdcard.img` to the directory where the emulator is, it will load the sd file when startup.