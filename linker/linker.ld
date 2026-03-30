ENTRY(_start)

PHDRS {
    boot_text PT_LOAD FLAGS(5);   /* R|X */
    boot_rodata PT_LOAD FLAGS(4); /* R */
    boot_data PT_LOAD FLAGS(6);   /* R|W */
    text PT_LOAD FLAGS(5);        /* R|X */
    rodata PT_LOAD FLAGS(4);      /* R */
    data PT_LOAD FLAGS(6);        /* R|W */
}

SECTIONS {
    KERNEL_PHYS_BASE = 1M;
    KERNEL_VIRT_BASE = 0xC0000000;

    /* low bootstrap runs here before paging */
    . = KERNEL_PHYS_BASE;
    _kernel_phys_start = .;

    .multiboot : {
        KEEP(*(.multiboot))
    } :boot_text

    .text.boot : {
        *(.text.boot*)
    } :boot_text

    .rodata.boot : {
        *(.rodata.boot*)
    } :boot_rodata

    .data.boot : {
        *(.data.boot*)
    } :boot_data

    .bss.boot (NOLOAD) : {
        *(.bss.boot*)
    } :boot_data

    . = ALIGN(4K);
    __boot_phys_end = .;

    /* higher-half VMA, physical LMA */
    . = __boot_phys_end + KERNEL_VIRT_BASE;
    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE) {
        _kernel_text_start = .;
        *(.text*)
        _kernel_text_end = .;
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE) {
        _kernel_rodata_start = .;
        *(.rodata*)
        _kernel_rodata_end = .;
    } :rodata

    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE) {
        *(.data*)
    } :data

    . = ALIGN(64K);
    .dma : AT(ADDR(.dma) - KERNEL_VIRT_BASE) {
        *(.dma)
    } :data

    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_VIRT_BASE) {
        *(.bss*)
        *(COMMON)
    } :data

    _kernel_end = .;
    _kernel_phys_end = _kernel_end - KERNEL_VIRT_BASE;
}
