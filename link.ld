OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    
    .multiboot ALIGN(8) : {
    *(.note.Xen)
        *(.multiboot2)
    }
    
    .boot ALIGN(4K) : {
        *(.text.boot)
    }
    
    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }
    
    .ctors ALIGN(8) : {
        __CTOR_LIST__ = .;
        LONG((__CTOR_END__ - __CTOR_LIST__) / 8 - 2)
        KEEP (*(.ctors))
        LONG(0)
        __CTOR_END__ = .;
    }
    
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
