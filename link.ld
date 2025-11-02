ENTRY(_start)

SECTIONS
{
    . = 1M;
    
    .note.Xen ALIGN(4) :
    {
        *(.note.Xen)
    }
    
    /* FIXED: Combine all boot sections into ONE output section */
    .boot ALIGN(4K) :
    {
        *(.multiboot2)
        *(.text.boot)
        . = ALIGN(4K);
        *(.boot.rodata)
        . = ALIGN(16);
        *(.boot.data)      /* ← GDT and GDT pointer here */
    }
    
    .boot.bss ALIGN(4K) :
    {
        __boot_bss_start = .;
        *(.boot.bss)
        __boot_bss_end = .;
    }
    
    .boot.stack ALIGN(4K) (NOLOAD) :
    {
        *(.boot.stack)
    }
    
    __kernel_physical_start = .;
    . += 0xFFFFFFFF80000000;
    
    .text ALIGN(4K) : AT(__kernel_physical_start)
    {
        __kernel_start = .;
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xFFFFFFFF80000000)
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(4K) : AT(ADDR(.data) - 0xFFFFFFFF80000000)
    {
        *(.data)
        *(.data.*)
    }
    
    .ctors ALIGN(8) : AT(ADDR(.ctors) - 0xFFFFFFFF80000000)
    {
        KEEP(*(.ctors))
    }
    
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xFFFFFFFF80000000)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
    
    __kernel_end = .;
    
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}
