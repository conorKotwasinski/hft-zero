# kernel/isr.S - Interrupt Service Routine stubs

.section .text
.code64

# Macro for ISR without error code
.macro ISR_NOERRCODE num
.global isr\num
.type isr\num, @function
.align 16
isr\num:
    pushq $0        # Push dummy error code
    pushq $\num     # Push interrupt number
    jmp isr_common
.endm

# Macro for ISR with error code
.macro ISR_ERRCODE num
.global isr\num
.type isr\num, @function
.align 16
isr\num:
    pushq $\num     # Push interrupt number
    jmp isr_common
.endm

# Macro for IRQ handlers
.macro IRQ num, int_num
.global irq\num
.type irq\num, @function
.align 16
irq\num:
    pushq $0        # No error code for IRQs
    pushq $\int_num # Push interrupt number (32 + IRQ number)
    jmp isr_common
.endm

# Exception handlers (0-31)
ISR_NOERRCODE 0   # Division by zero
ISR_NOERRCODE 1   # Debug
ISR_NOERRCODE 2   # NMI
ISR_NOERRCODE 3   # Breakpoint
ISR_NOERRCODE 4   # Overflow
ISR_NOERRCODE 5   # Bound range exceeded
ISR_NOERRCODE 6   # Invalid opcode
ISR_NOERRCODE 7   # Device not available
ISR_ERRCODE   8   # Double fault
ISR_NOERRCODE 9   # Coprocessor segment overrun
ISR_ERRCODE   10  # Invalid TSS
ISR_ERRCODE   11  # Segment not present
ISR_ERRCODE   12  # Stack segment fault
ISR_ERRCODE   13  # General protection fault
ISR_ERRCODE   14  # Page fault
ISR_NOERRCODE 15  # Reserved
ISR_NOERRCODE 16  # x87 FPU error
ISR_ERRCODE   17  # Alignment check
ISR_NOERRCODE 18  # Machine check
ISR_NOERRCODE 19  # SIMD floating point
ISR_NOERRCODE 20  # Virtualization
ISR_ERRCODE   21  # Control protection
ISR_NOERRCODE 22  # Reserved
ISR_NOERRCODE 23  # Reserved
ISR_NOERRCODE 24  # Reserved
ISR_NOERRCODE 25  # Reserved
ISR_NOERRCODE 26  # Reserved
ISR_NOERRCODE 27  # Reserved
ISR_NOERRCODE 28  # Hypervisor injection
ISR_NOERRCODE 29  # VMM communication
ISR_NOERRCODE 30  # Security exception
ISR_NOERRCODE 31  # Reserved

# IRQ handlers (32-47)
IRQ 0, 32   # Timer
IRQ 1, 33   # Keyboard
IRQ 2, 34   # Cascade
IRQ 3, 35   # COM2
IRQ 4, 36   # COM1
IRQ 5, 37   # LPT2
IRQ 6, 38   # Floppy
IRQ 7, 39   # LPT1
IRQ 8, 40   # RTC
IRQ 9, 41   # Free
IRQ 10, 42  # Free
IRQ 11, 43  # Free
IRQ 12, 44  # Mouse
IRQ 13, 45  # FPU
IRQ 14, 46  # Primary ATA
IRQ 15, 47  # Secondary ATA

# Common ISR handler
.extern interrupt_handler_common
.type isr_common, @function
isr_common:
    # Save all registers
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    # Save segment registers
    movq %ds, %rax
    pushq %rax
    movq %es, %rax
    pushq %rax
    movq %fs, %rax
    pushq %rax
    movq %gs, %rax
    pushq %rax
    
    # Load kernel data segment
    movq $0x10, %rax
    movq %rax, %ds
    movq %rax, %es
    movq %rax, %fs
    movq %rax, %gs
    
    # Call C handler with pointer to saved context
    movq %rsp, %rdi
    call interrupt_handler_common
    
    # Restore segment registers
    popq %rax
    movq %rax, %gs
    popq %rax
    movq %rax, %fs
    popq %rax
    movq %rax, %es
    popq %rax
    movq %rax, %ds
    
    # Restore all registers
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    
    # Clean up error code and interrupt number
    addq $16, %rsp
    
    # Return from interrupt
    iretq
