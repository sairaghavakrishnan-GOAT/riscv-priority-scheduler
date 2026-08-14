/* Linker script for QEMU virt machine (RV32IMA), machine-mode only, no OS */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

SECTIONS
{
    . = 0x80000000;

    .text : {
        *(.text.init)
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    .data : {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    } > RAM

    .bss : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    . = ALIGN(16);
    _heap_start = .;

    /* Boot stack, used only before the first task starts running */
    . = . + 0x2000;
    _boot_stack_top = .;
}
