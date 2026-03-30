ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);   /* R|X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* R|W */
}

SECTIONS {
    . = 0x00400000;

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :rodata

    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data
}
