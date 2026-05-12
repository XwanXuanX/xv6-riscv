# ==============================================================================
# RISC-V Cross-Compilation Toolchain
# ==============================================================================

# Try to find the RISC-V toolchain.
find_program(RISCV_GCC riscv64-unknown-elf-gcc)

if(NOT RISCV_GCC)
    find_program(RISCV_GCC riscv64-elf-gcc)
endif()

if(NOT RISCV_GCC)
    find_program(RISCV_GCC riscv64-none-elf-gcc)
endif()

if(NOT RISCV_GCC)
    find_program(RISCV_GCC riscv64-linux-gnu-gcc)
endif()

if(NOT RISCV_GCC)
    find_program(RISCV_GCC riscv64-unknown-linux-gnu-gcc)
endif()

if(NOT RISCV_GCC)
    find_program(RISCV_GCC riscv-none-elf-gcc)
endif()

if(NOT RISCV_GCC)
    message(FATAL_ERROR
            "Could not find a RISC-V GCC cross-compiler.\n"
            "Searched for: riscv64-unknown-elf-gcc, riscv64-elf-gcc, riscv64-none-elf-gcc, "
            "riscv64-linux-gnu-gcc, riscv64-unknown-linux-gnu-gcc.\n"
            "Please install an appropriate RISC-V GCC toolchain and ensure the compiler is in your PATH, "
            "or rerun CMake with -DRISCV_GCC=/full/path/to/riscv64-unknown-elf-gcc (or equivalent)."
    )
endif()

# ==============================================================================
# Derive Toolchain Paths
# ==============================================================================

get_filename_component(RISCV_TOOLCHAIN_BIN_DIR "${RISCV_GCC}" DIRECTORY)
get_filename_component(RISCV_TOOLCHAIN_DIR "${RISCV_TOOLCHAIN_BIN_DIR}" DIRECTORY)

# Extract the toolchain prefix.
get_filename_component(TOOLPREFIX "${RISCV_GCC}" NAME)
string(REGEX REPLACE "gcc$" "" TOOLPREFIX "${TOOLPREFIX}")

# ==============================================================================
# Target System
# ==============================================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# ==============================================================================
# Compilers and Binary Utilities
# ==============================================================================

set(CMAKE_C_COMPILER "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}gcc")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}g++")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}gcc")

set(CMAKE_OBJCOPY "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}objcopy")
set(CMAKE_OBJDUMP "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}objdump")
set(CMAKE_AR "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}ar")
set(CMAKE_RANLIB "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}ranlib")
set(CMAKE_LINKER "${RISCV_TOOLCHAIN_BIN_DIR}/${TOOLPREFIX}ld")

# ==============================================================================
# CMake Search Behavior
# ==============================================================================

set(CMAKE_FIND_ROOT_PATH "${RISCV_TOOLCHAIN_DIR}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Avoid running target binaries during compiler checks.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)