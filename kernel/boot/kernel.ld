OUTPUT_ARCH( "riscv" )
/* set the program's entry symbol, sets the ELF entry point */
ENTRY( _entry )

/*
 * Some important notation:
 * 1. `.` is the current output address
 * 2. Each output section collects matching input sections from all your .o files and places them contiguously in memory
 * 3. ALIGN(n) moves . forward to the next multiple of n
 */

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   * NOTE: physical address 0x0 is not DRAM. DRAM is placed at a high physical base.
   * So there often is no usable RAM “before” the kernel. We are fine and didn't waste space.
   */
  . = 0x80000000;

  .text : {
    /* ensures the `_entry` symbol (from entry.o) is placed first in .text, right at 0x80000000 */
    /* so CPU lands exactly on `_entry` symbol when it jumps */
    */entry.o(_entry)
    /* Pulls in all normal code from every object file */
    *(.text .text.*)

    /* makes sure the trampoline code lives on its own page, PAGE_SIZE = 0x1000 */
    . = ALIGN(0x1000);
    _trampoline = .; /* define a symbol marking the trampoline start */
    *(trampsec)      /* include all input sections named trampsec */
    . = ALIGN(0x1000);
    /* alignment done! */

    /* ensures the trampoline took exactly one page */
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    /* `end of text` address symbol */
    PROVIDE(etext = .);
  }

  /* read-only data: string literals, const globals, etc. */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  /* writable globals/statics that have an initial value */
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  /* holds globals/statics that should start as zero, e.g. int counters[256]; */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  /*
   * `end` is a symbol (a named address) that the linker defines
   * `.` is the linker location counter: the current output address at that point in the script
   * `end` becomes the first byte after the kernel image in RAM (after kernel reserved memory)
   */
  PROVIDE(end = .);
}
