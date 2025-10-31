ENTRY(_start)

SECTIONS
{
    /* Kernel loads at 1MB physical, but runs at high virtual address */
    . = 1M;
    
    /* Multiboot header must be early in the file */
    .boot ALIGN(4K) :
    {
        *(.multiboot2)
        *(.text.boot)
    }
    
    /* Move to higher half virtual address */
    . += 0xFFFFFFFF80000000;
    
    .text ALIGN(4K) : AT(ADDR(.text) - 0xFFFFFFFF80000000)
    {
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
    
    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xFFFFFFFF80000000)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
    
    .stack_guard ALIGN(4K) : AT(ADDR(.stack_guard) - 0xFFFFFFFF80000000)
    {
        *(.stack_guard)
    }
    
    /* End of kernel */
    __kernel_end = .;
    
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.build-id)
    }
}
