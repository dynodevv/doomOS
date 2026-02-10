/*
 * linker.ld — Higher-Half Kernel Linker Script for doomOS
 *
 * Places the kernel at the higher-half virtual address (0xFFFFFFFF80000000).
 * All sections are 2 MB aligned for compatibility with 2 MB huge pages.
 * The .wad section holds the embedded DOOM1.WAD binary blob.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

PHDRS
{
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));          /* r-x */
    rodata  PT_LOAD FLAGS((1 << 2));                      /* r-- */
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));          /* rw- */
}

SECTIONS
{
    . = KERNEL_VIRT_BASE;

    .text ALIGN(0x200000) : AT(ADDR(.text) - KERNEL_VIRT_BASE)
    {
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x200000) : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
    {
        *(.rodata .rodata.*)
    } :rodata

    /* Embedded WAD file — symbols exposed to C code */
    .wad ALIGN(0x200000) : AT(ADDR(.wad) - KERNEL_VIRT_BASE)
    {
        _binary_DOOM1_WAD_start = .;
        *(.wad)
        _binary_DOOM1_WAD_end = .;
    } :rodata

    .data ALIGN(0x200000) : AT(ADDR(.data) - KERNEL_VIRT_BASE)
    {
        *(.data .data.*)
    } :data

    /*
     * Limine request markers — bootloader writes response pointers into
     * these structures, so they MUST be in a writable (rw-) segment.
     */
    .limine_requests_start ALIGN(0x1000) : AT(ADDR(.limine_requests_start) - KERNEL_VIRT_BASE)
    {
        KEEP(*(.limine_requests_start))
    } :data

    .limine_requests ALIGN(0x1000) : AT(ADDR(.limine_requests) - KERNEL_VIRT_BASE)
    {
        KEEP(*(.limine_requests))
    } :data

    .limine_requests_end ALIGN(0x1000) : AT(ADDR(.limine_requests_end) - KERNEL_VIRT_BASE)
    {
        KEEP(*(.limine_requests_end))
    } :data

    .bss ALIGN(0x200000) : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    } :data

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}
