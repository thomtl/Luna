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
        
        . = ALIGN(8);
        _usb_drivers_start = .;
        KEEP(*(.usb_drivers*))
        _usb_drivers_end = .;

        . = ALIGN(8);
        _pci_drivers_start = .;
        KEEP(*(.pci_drivers*))
        _pci_drivers_end = .;
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