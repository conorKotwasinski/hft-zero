# boot.S - Multiboot2 compliant bootloader

.section .multiboot2, "a"
.align 8
multiboot2_header:
    .long 0xE85250D6                # Magic number (multiboot2)
    .long 0                          # Architecture (i386)
    .long multiboot2_header_end - multiboot2_header  # Header length
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))  # Checksum

    # End tag
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 16384  # 16KB stack
stack_top:

.section .text.boot
.global _start
.type _start, @function
_start:
    # Load stack pointer
    movl $stack_top, %esp
    
    # Reset EFLAGS
    pushl $0
    popf
    
    # Push multiboot info
    pushl %eax  # Multiboot magic
    pushl %ebx  # Multiboot info pointer
    
    # Call kernel main
    call kernel_main
    
    # Halt if kernel returns (it shouldn't)
hang:
    cli
    hlt
    jmp hang

.size _start, . - _start
