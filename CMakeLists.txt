cmake_minimum_required(VERSION 3.20)

# Set the toolchain file before project()
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake)

project(xv6-riscv C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common flags for all targets
set(COMMON_FLAGS
    -Wall
    -Werror
    -Wno-unknown-attributes
    -O2
    -fno-omit-frame-pointer
    -ggdb
    -gdwarf-2
    -march=rv64gc
    -MD
    -mcmodel=medany
    -ffreestanding
    -fno-common
    -nostdlib
    -fno-builtin-strncpy
    -fno-builtin-strncmp
    -fno-builtin-strlen
    -fno-builtin-memset
    -fno-builtin-memmove
    -fno-builtin-memcmp
    -fno-builtin-log
    -fno-builtin-bzero
    -fno-builtin-strchr
    -fno-builtin-exit
    -fno-builtin-malloc
    -fno-builtin-putc
    -fno-builtin-free
    -fno-builtin-memcpy
    -Wno-main
    -fno-builtin-printf
    -fno-builtin-fprintf
    -fno-builtin-vprintf
    -I${CMAKE_CURRENT_SOURCE_DIR}
)

# Check for stack protector support
include(CheckCCompilerFlag)
check_c_compiler_flag(-fno-stack-protector HAS_NO_STACK_PROTECTOR)
if(HAS_NO_STACK_PROTECTOR)
    list(APPEND COMMON_FLAGS -fno-stack-protector)
endif()

# Check for PIE support
check_c_compiler_flag(-fno-pie HAS_NO_PIE)
if(HAS_NO_PIE)
    list(APPEND COMMON_FLAGS -fno-pie -no-pie)
endif()

# C++ specific flags
set(CXX_FLAGS
    ${COMMON_FLAGS}
    -std=c++20
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
)

# Kernel sources
set(KERNEL_SOURCES
    kernel/entry.S
    kernel/start.cc
    kernel/console.cc
    kernel/printf.cc
    kernel/uart.cc
    kernel/kalloc.cc
    kernel/slab.cc
    kernel/slab_selftest.cc
    kernel/spinlock.cc
    kernel/string.cc
    kernel/main.cc
    kernel/vm.cc
    kernel/proc.cc
    kernel/proc_list.cc
    kernel/kstack_alloc.cc
    kernel/kstack_alloc_selftest.cc
    kernel/stl/ts_forward_list_selftest.cc
    kernel/swtch.S
    kernel/trampoline.S
    kernel/trap.cc
    kernel/syscall.cc
    kernel/sysproc.cc
    kernel/bio.cc
    kernel/fs.cc
    kernel/log.cc
    kernel/sleeplock.cc
    kernel/file.cc
    kernel/pipe.cc
    kernel/exec.cc
    kernel/sysfile.cc
    kernel/kernelvec.S
    kernel/plic.cc
    kernel/virtio_disk.cc
    kernel/mlfq.cc
)

# Create an OBJECT library for CLion code navigation and completion
# This doesn't produce an actual library, just compiles the sources for IDE support
add_library(kernel_ide OBJECT EXCLUDE_FROM_ALL ${KERNEL_SOURCES})

target_compile_options(kernel_ide PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${COMMON_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:-march=rv64gc -g>
)

# Kernel object files for actual build
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/kernel)

set(KERNEL_OBJECTS)
foreach(src ${KERNEL_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    get_filename_component(ext ${src} EXT)
    
    if(ext STREQUAL ".S")
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/kernel/${name}.o)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_ASM_COMPILER} -march=rv64gc -g -c -o ${obj} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMENT "Building ASM object ${name}.o"
        )
    elseif(ext STREQUAL ".c")
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/kernel/${name}.o)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_C_COMPILER} ${COMMON_FLAGS} -c -o ${obj} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMENT "Building C object ${name}.o"
        )
    elseif(ext STREQUAL ".cc")
        set(obj ${CMAKE_CURRENT_BINARY_DIR}/kernel/${name}.o)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_CXX_COMPILER} ${CXX_FLAGS} -c -o ${obj} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMENT "Building CXX object ${name}.o"
        )
    endif()
    
    list(APPEND KERNEL_OBJECTS ${obj})
endforeach()

# Link kernel with custom command
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel
    COMMAND ${CMAKE_LINKER} -z max-page-size=4096 -T ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.ld -o ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel ${KERNEL_OBJECTS}
    DEPENDS ${KERNEL_OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.ld
    COMMENT "Linking kernel"
)

# Generate kernel.asm and kernel.sym
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.asm ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.sym
    COMMAND ${CMAKE_OBJDUMP} -S ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel > ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.asm
    COMMAND ${CMAKE_OBJDUMP} -t ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel | sed '1,/SYMBOL TABLE/d$<SEMICOLON> s/ .* / /$<SEMICOLON> /^$$/d' > ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.sym
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel
    COMMENT "Generating kernel.asm and kernel.sym"
)

# Single target that depends on both kernel binary and its disassembly
add_custom_target(kernel ALL 
    DEPENDS 
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.asm 
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel.sym
)

# User library
add_library(ulib OBJECT
    user/ulib.cc
    user/usys.S
    user/printf.cc
    user/umalloc.cc
)

target_compile_options(ulib PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${COMMON_FLAGS}>
)

# Generate usys.S
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/user/usys.S
    COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/user/usys.pl > ${CMAKE_CURRENT_SOURCE_DIR}/user/usys.S
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/user/usys.pl
    COMMENT "Generating usys.S"
)

# User programs
set(USER_PROGS
    cat echo grep init kill ln ls mkdir rm sh stressfs
    grind wc zombie logstress forphan dorphan hellocpp mlfq_test
)

# Special handling for usertests (not in initial list but in Makefile)
list(APPEND USER_PROGS usertests)

foreach(prog ${USER_PROGS})
    add_executable(_${prog} user/${prog}.cc)
    
    target_compile_options(_${prog} PRIVATE ${CXX_FLAGS})
    
    target_link_options(_${prog} PRIVATE
        -T ${CMAKE_CURRENT_SOURCE_DIR}/user/user.ld
        -z max-page-size=4096
        -nostdlib
    )
    
    target_link_libraries(_${prog} PRIVATE ulib)
    
    set_target_properties(_${prog} PROPERTIES
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/user/user.ld
        OUTPUT_NAME _${prog}
        SUFFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/user
    )
    
    # Generate .asm and .sym files
    add_custom_command(TARGET _${prog} POST_BUILD
        COMMAND ${CMAKE_OBJDUMP} -S $<TARGET_FILE:_${prog}> > ${CMAKE_CURRENT_SOURCE_DIR}/user/${prog}.asm
        COMMAND ${CMAKE_OBJDUMP} -t $<TARGET_FILE:_${prog}> | sed '1,/SYMBOL TABLE/d$<SEMICOLON> s/ .* / /$<SEMICOLON> /^$$/d' > ${CMAKE_CURRENT_SOURCE_DIR}/user/${prog}.sym
        COMMENT "Generating ${prog}.asm and ${prog}.sym"
    )
endforeach()

# Special build for forktest (smaller binary)
add_executable(_forktest user/forktest.cc)

target_compile_options(_forktest PRIVATE ${CXX_FLAGS})

target_link_options(_forktest PRIVATE
    -N
    -e main
    -Ttext 0
    -nostdlib
)

target_link_libraries(_forktest PRIVATE ulib)

set_target_properties(_forktest PROPERTIES
    OUTPUT_NAME _forktest
    SUFFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/user
)

add_custom_command(TARGET _forktest POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -S $<TARGET_FILE:_forktest> > ${CMAKE_CURRENT_SOURCE_DIR}/user/forktest.asm
    COMMENT "Generating forktest.asm"
)

# mkfs tool (built with host compiler)
# We need to build this with the host compiler, not the cross-compiler
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/mkfs/mkfs
    COMMAND /usr/bin/c++ -Wno-unknown-attributes -I${CMAKE_CURRENT_SOURCE_DIR} -o ${CMAKE_CURRENT_SOURCE_DIR}/mkfs/mkfs ${CMAKE_CURRENT_SOURCE_DIR}/mkfs/mkfs.cc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mkfs/mkfs.cc
    COMMENT "Building mkfs with host compiler"
)

add_custom_target(mkfs ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mkfs/mkfs)

# Filesystem image
set(ALL_USER_PROGS)
foreach(prog ${USER_PROGS})
    list(APPEND ALL_USER_PROGS user/_${prog})
endforeach()
list(APPEND ALL_USER_PROGS user/_forktest)

# Full paths for dependencies
set(ALL_USER_PROGS_FULL)
foreach(prog ${USER_PROGS})
    list(APPEND ALL_USER_PROGS_FULL ${CMAKE_CURRENT_SOURCE_DIR}/user/_${prog})
endforeach()
list(APPEND ALL_USER_PROGS_FULL ${CMAKE_CURRENT_SOURCE_DIR}/user/_forktest)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/fs.img
    COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ./mkfs/mkfs fs.img README.md ${ALL_USER_PROGS}
    DEPENDS mkfs ${ALL_USER_PROGS_FULL} ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    COMMENT "Creating filesystem image"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(fs_img ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fs.img)

# QEMU target
find_program(QEMU_EXECUTABLE qemu-system-riscv64)
if(NOT QEMU_EXECUTABLE)
    message(WARNING "qemu-system-riscv64 not found. 'make qemu' target will not be available.")
else()
    set(CPUS 3 CACHE STRING "Number of CPUs for QEMU")
    
    add_custom_target(qemu
        COMMAND ${QEMU_EXECUTABLE}
            -machine virt
            -bios none
            -kernel ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel
            -m 128M
            -smp ${CPUS}
            -nographic
            -global virtio-mmio.force-legacy=false
            -drive file=${CMAKE_CURRENT_SOURCE_DIR}/fs.img,if=none,format=raw,id=x0
            -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
        DEPENDS kernel fs_img
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running xv6 in QEMU"
        USES_TERMINAL
    )
    
    # QEMU with GDB
    execute_process(
        COMMAND id -u
        OUTPUT_VARIABLE USER_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    math(EXPR GDBPORT "${USER_ID} % 5000 + 25000")
    
    add_custom_target(qemu-gdb
        COMMAND ${QEMU_EXECUTABLE}
            -machine virt
            -bios none
            -kernel ${CMAKE_CURRENT_SOURCE_DIR}/kernel/kernel
            -m 128M
            -smp ${CPUS}
            -nographic
            -global virtio-mmio.force-legacy=false
            -drive file=${CMAKE_CURRENT_SOURCE_DIR}/fs.img,if=none,format=raw,id=x0
            -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
            -S
            -gdb tcp::${GDBPORT}
        DEPENDS kernel fs_img
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Running xv6 in QEMU with GDB on port ${GDBPORT}"
        USES_TERMINAL
    )
    
    message(STATUS "QEMU GDB port will be: ${GDBPORT}")
endif()
