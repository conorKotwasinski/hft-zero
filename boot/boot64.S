# boot/boot64.S - FINAL FIX: Correct 32-bit GDT pointer
# All bugs fixed: stack separation, byte encoding, PAE order, and GDT pointer size

# PVH boot note
.section .note.Xen, "a"
.align 4
pvh_note_start:
    .long 4
    .long 12
    .long 18
    .asciz "Xen"
    .align 4
    .long _start
    .long 0
    .long 0
pvh_note_end:

# Multiboot2 header
.section .multiboot2, "a"
.align 8
multiboot2_header:
    .long 0xE85250D6
    .long 0
    .long multiboot2_header_end - multiboot2_header
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))
    
    .align 8
    .word 6
    .word 0
    .long 8
    
    .align 8
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

# Boot BSS - page tables only, NO STACK
.section .boot.bss, "aw", @nobits
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

# Boot stack - SEPARATE section (critical fix!)
.section .boot.stack, "aw", @nobits
.align 4096
stack_bottom:
    .skip 32768
stack_top:

# Boot GDT
.section .boot.data
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
    .quad 0x0000000000000000
gdt64_end:

# CRITICAL FIX: 32-bit GDT pointer (6 bytes: 2-byte limit + 4-byte base)
.align 4
gdt64_ptr_32:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .text.boot
.code32
.global _start
.type _start, @function
_start:
    cli
    movl %eax, %edi
    movl %ebx, %esi
    movl $stack_top, %esp
    call clear_bss
    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging
    
    # Load GDT using 32-bit pointer!
    lgdt gdt64_ptr_32
    
    # Manual far jump
    .byte 0xEA
    .long start64_low
    .word 0x08

# Clear BSS (page tables only, stack is safe)
clear_bss:
    movl $__boot_bss_start, %ecx
    movl $__boot_bss_end, %edx
1:
    cmpl %edx, %ecx
    jae 2f
    movb $0, (%ecx)
    addl $1, %ecx
    jmp 1b
2:
    ret

# BYTE-ENCODED check_cpuid
check_cpuid:
    .byte 0x9C                # pushfl
    .byte 0x58                # popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    .byte 0x50                # pushl %eax
    .byte 0x9D                # popfl
    .byte 0x9C                # pushfl
    .byte 0x58                # popl %eax
    .byte 0x51                # pushl %ecx
    .byte 0x9D                # popfl
    cmpl %eax, %ecx
    je no_cpuid
    ret

no_cpuid:
    movl $err_no_cpuid, %esi
    jmp error32

check_long_mode:
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode
    
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz no_long_mode
    
    movl $1, %eax
    cpuid
    testl $0x40, %edx
    jz no_pae
    
    testl $0x04000000, %edx
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

setup_page_tables:
    movl $boot_pml4, %edi
    movl $0, %eax
    movl $5120, %ecx
    rep stosl
    
    movl $boot_pdpt_low, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4
    
    movl $boot_pdpt_high, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4 + 511*8
    
    movl $boot_pdt, %eax
    orl $0x03, %eax
    movl %eax, boot_pdpt_low
    movl %eax, boot_pdpt_high + 510*8
    
    movl $boot_pdt, %edi
    movl $0x83, %eax
    movl $512, %ecx
1:
    movl %eax, (%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b
    
    ret

# CORRECT ORDER: PAE first, then CR3, then EFER, then paging
enable_paging:
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4
    
    movl $boot_pml4, %eax
    movl %eax, %cr3
    
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0
    
    ret

error32:
    movl $0xB8000, %edi
    movl $0x4F524F45, (%edi)
    movl $0x4F4F4F52, 4(%edi)
    movl $0x4F3A4F52, 8(%edi)
    movl $0x4F204F20, 12(%edi)
    
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

.section .boot.rodata
err_no_cpuid:      .asciz "CPUID not supported"
err_no_long_mode:  .asciz "64-bit mode not supported"
err_no_pae:        .asciz "PAE not supported"
err_no_sse2:       .asciz "SSE2 not supported"

.section .text.boot
.code64
start64_low:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movabsq $start64_high, %rax
    jmp *%rax

.section .text
.code64
start64_high:
    movabsq $stack_top, %rsp
    movabsq $0xFFFFFFFF80000000, %rax
    addq %rax, %rsp
    movq %cr0, %rax
    andq $~0x04, %rax
    orq $0x02, %rax
    movq %rax, %cr0
    movq %cr4, %rax
    orq $0x600, %rax
    movq %rax, %cr4
    movq %rdi, %rdi
    movq %rsi, %rsi
    movabsq $kernel_main, %rax
    call *%rax
halt:
    cli
    hlt
    jmp halt

.global __boot_bss_start
.global __boot_bss_end
