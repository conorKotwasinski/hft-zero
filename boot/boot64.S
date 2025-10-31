# boot/boot64.S - Complete 64-bit boot sequence with higher-half kernel

.section .multiboot2, "a"
.align 8
multiboot2_header:
    .long 0xE85250D6                # Magic number
    .long 0                          # Architecture (i386)
    .long multiboot2_header_end - multiboot2_header
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))
    
    # Request aligned modules
    .align 8
    .word 6     # Type: module alignment
    .word 0     # Flags
    .long 8     # Size
    
    # End tag
    .align 8
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .bss
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt_low:
    .skip 4096
boot_pdpt_high:
    .skip 4096
boot_pdt:
    .skip 4096
boot_pt:
    .skip 4096
stack_bottom:
    .skip 32768  # 32KB stack
stack_top:

.section .data
.align 8
gdt64:
    .quad 0x0000000000000000  # Null
    .quad 0x00AF9A000000FFFF  # Code (64-bit, DPL0)
    .quad 0x00AF92000000FFFF  # Data
    .quad 0x00AF9A000000FFFF  # User code (DPL3)
    .quad 0x00AF92000000FFFF  # User data
    .quad 0x0000000000000000  # TSS (will be filled later)
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .text.boot
.code32
.global _start
.type _start, @function
_start:
    # Disable interrupts
    cli
    
    # Save multiboot info
    movl %eax, %edi
    movl %ebx, %esi
    
    # Set stack
    movl $stack_top, %esp
    
    # Clear .bss
    call clear_bss
    
    # Check CPU features
    call check_cpuid
    call check_long_mode
    
    # Set up paging for higher-half kernel
    call setup_page_tables
    call enable_paging
    
    # Load 64-bit GDT
    lgdt gdt64_ptr
    
    # Jump to 64-bit code
    ljmp $0x08, $start64_low

# Clear BSS section
clear_bss:
    movl $__bss_start, %ecx
    movl $__bss_end, %edx
1:
    cmpl %ecx, %edx
    je 2f
    movb $0, (%ecx)
    incl %ecx
    jmp 1b
2:
    ret

# Check if CPUID is supported
check_cpuid:
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %eax, %ecx
    je no_cpuid
    ret

no_cpuid:
    movl $err_no_cpuid, %esi
    jmp error32

# Check for long mode support
check_long_mode:
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode
    
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx  # LM bit
    jz no_long_mode
    
    # Check for PAE
    movl $1, %eax
    cpuid
    testl $0x40, %edx  # PAE bit
    jz no_pae
    
    # Check for SSE2 (required for 64-bit)
    testl $0x04000000, %edx  # SSE2 bit
    jz no_sse2
    
    ret

no_long_mode:
    movl $err_no_long_mode, %esi
    jmp error32

no_pae:
    movl $err_no_pae, %esi
    jmp error32

no_sse2:
    movl $err_no_sse2, %esi
    jmp error32

# Set up 4-level page tables
# Identity map first 1GB and map kernel to 0xFFFFFFFF80000000
setup_page_tables:
    # Clear page tables
    movl $boot_pml4, %edi
    movl $0, %eax
    movl $5120, %ecx  # 5 pages * 1024 dwords
    rep stosl
    
    # PML4[0] -> PDPT_low (identity mapping)
    movl $boot_pdpt_low, %eax
    orl $0x03, %eax  # Present + Writable
    movl %eax, boot_pml4
    
    # PML4[511] -> PDPT_high (higher half)
    movl $boot_pdpt_high, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4 + 511*8
    
    # PDPT_low[0] -> PDT
    movl $boot_pdt, %eax
    orl $0x03, %eax
    movl %eax, boot_pdpt_low
    
    # PDPT_high[510] -> PDT (maps to 0xFFFFFFFF80000000)
    movl %eax, boot_pdpt_high + 510*8
    
    # Map first 1GB using 2MB pages
    movl $boot_pdt, %edi
    movl $0x83, %eax  # Present + Writable + Large
    movl $512, %ecx   # 512 * 2MB = 1GB
1:
    movl %eax, (%edi)
    addl $0x200000, %eax  # 2MB
    addl $8, %edi
    loop 1b
    
    ret

# Enable paging and long mode
enable_paging:
    # Load PML4 into CR3
    movl $boot_pml4, %eax
    movl %eax, %cr3
    
    # Enable PAE
    movl %cr4, %eax
    orl $0x20, %eax  # PAE bit
    movl %eax, %cr4
    
    # Enable long mode
    movl $0xC0000080, %ecx  # EFER MSR
    rdmsr
    orl $0x100, %eax  # LME bit
    wrmsr
    
    # Enable paging
    movl %cr0, %eax
    orl $0x80000000, %eax  # PG bit
    movl %eax, %cr0
    
    ret

# Error handler for 32-bit mode
error32:
    movl $0xB8000, %edi
    movl $0x4F524F45, (%edi)    # "ER"
    movl $0x4F4F4F52, 4(%edi)   # "RO"
    movl $0x4F3A4F52, 8(%edi)   # "R:"
    movl $0x4F204F20, 12(%edi)  # "  "
    
    # Print error string
    movl $16, %edi
1:
    movb (%esi), %al
    testb %al, %al
    jz 2f
    movb %al, 0xB8000(%edi)
    movb $0x4F, 0xB8001(%edi)
    addl $2, %edi
    incl %esi
    jmp 1b
2:
    cli
    hlt
    jmp 2b

.section .rodata
err_no_cpuid:      .asciz "CPUID not supported"
err_no_long_mode:  .asciz "64-bit mode not supported"
err_no_pae:        .asciz "PAE not supported"
err_no_sse2:       .asciz "SSE2 not supported"

.section .text.boot
.code64
start64_low:
    # We're in 64-bit but still in lower memory
    # Set up segments
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    # Jump to higher half
    movabsq $start64_high, %rax
    jmp *%rax

.section .text
.code64
start64_high:
    # Now we're in the higher half
    # Set up stack in higher half
    movabsq $stack_top, %rsp
    
    # Enable SSE (required for C++)
    movq %cr0, %rax
    andq $~0x04, %rax  # Clear EM bit
    orq $0x02, %rax    # Set MP bit
    movq %rax, %cr0
    
    movq %cr4, %rax
    orq $0x600, %rax   # Set OSFXSR and OSXMMEXCPT
    movq %rax, %cr4
    
    # Pass multiboot info to kernel_main
    movq %rdi, %rdi  # Magic number (already in rdi)
    movq %rsi, %rsi  # Multiboot info (already in rsi)
    
    # Call kernel
    movabsq $kernel_main, %rax
    call *%rax
    
    # Halt if kernel returns
halt:
    cli
    hlt
    jmp halt

# Export symbols
.global __bss_start
.global __bss_end
