SECTIONS {
    . = 0xffffffff80100000;

    _kernel_start = .;
    .stivalehdr : ALIGN(4K) {
        *(.stivalehdr)
    }

    .text : ALIGN(4K) {
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)

        *(.eh_frame)
		*(.eh_frame_hdr)
    }

    .bss : ALIGN(4K) {
        *(.bss*)
        *(COMMON)
    }
    _kernel_end = .;
}