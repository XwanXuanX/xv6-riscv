# Building xv6-riscv with CMake

## Command Line

```bash
# Create build directory
mkdir build
cd build

# Configure (toolchain will be auto-detected)
cmake ..

# Build everything
cmake --build .

# Run in QEMU
cmake --build . --target qemu
```

## Available CMake Targets

- `kernel` - Build the xv6 kernel
- `_<program>` - Build individual user programs (e.g., `_sh`, `_ls`, `_usertests`)
- `fs_img` - Create the filesystem image
- `qemu` - Build everything and run in QEMU
- `qemu-gdb` - Run in QEMU with GDB server (for debugging)

## Debugging with GDB

```bash
# Terminal 1: Start QEMU with GDB server
cmake --build . --target qemu-gdb

# Terminal 2: Connect GDB
riscv64-unknown-elf-gdb kernel/kernel
(gdb) target remote :PORT  # Port is displayed when you run qemu-gdb
```
