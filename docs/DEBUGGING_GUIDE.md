# HFT-Zero Debugging Quick Reference

## Common Issues & Solutions

### Issue: Triple Fault / Immediate Reboot

**Symptoms:**
- QEMU reboots immediately
- Never see any output
- LLDB shows execution at 0xFFF0 repeatedly

**Causes & Fixes:**

1. **Page tables not set up correctly**
   ```bash
   # Debug: Check if page tables are being cleared
   lldb hft-zero.elf
   gdb-remote localhost:1234
   breakpoint set --address 0x101020  # _start
   breakpoint set --address 0x1010d9  # setup_page_tables
   continue
   ```

2. **Wrong paging enable order**
   - Must be: PAE → CR3 → EFER.LME → CR0.PG
   - Check boot64.S enable_paging function

3. **Invalid GDT**
   ```bash
   # Check GDT is loaded in memory
   x/48bx 0x103000  # Should see GDT entries, not zeros
   ```

---

### Issue: Hang at GDT Init

**Symptoms:**
- Boots, prints banner
- Hangs at "[*] Initializing GDT..."
- LLDB shows infinite loop or crash after lgdt

**Cause:**
- GDT descriptor size mismatch (8-byte vs 16-byte)

**Fix:**
- Skip gdt::init() - bootloader's GDT works fine
- Comment out in kernel/main_phase1.cpp

**Proper fix (future):**
```cpp
// Need separate 8-byte and 16-byte descriptor types
struct descriptor_8 { /* 8 bytes */ };
struct tss_descriptor { /* 16 bytes */ };
```

---

### Issue: Hang at VMM Init

**Symptoms:**
- Hangs at "[*] Initializing VMM..."
- LLDB shows loop in pmm::allocate_page()
- ticks never increment

**Cause:**
- PMM out of memory (only has 3MB)
- VMM trying to map 1GB (needs 262,144 pages)

**Fix:**
- Skip vmm::init() - use bootloader's page tables
- Or: Fix PMM to use real memory size first

---

### Issue: PMM Shows 0 Free Pages

**Symptoms:**
```
[*] Initializing PMM... 
    Free pages: 0 / 65536
[OK]
```

**Causes:**

1. **Not parsing multiboot memory map**
   ```cpp
   // Check if mmap_addr is null
   if (magic == 0x36d76289 && multiboot_info) {
       // Should find memory map
   }
   ```

2. **Memory map parsing fails**
   ```bash
   # Add debug output
   serial::puts("Tag type: ");
   serial::put_number(tag->type);
   serial::puts("\n");
   ```

3. **Kernel addresses wrong**
   ```cpp
   // Must convert virtual → physical
   uint64_t phys = virt - 0xFFFFFFFF80000000;
   ```

**Fix:**
- Ensure multiboot2 magic is correct
- Check tag->type == 6 for memory map
- Use init_fallback() if no map available

---

### Issue: Heap Init Fails

**Symptoms:**
- Hangs or crashes at "[*] Initializing heap..."

**Cause:**
- PMM has no free pages
- VMM can't allocate virtual memory

**Fix:**
- Fix PMM first (see above)
- Verify PMM shows >50,000 free pages

---

### Issue: No Timer Interrupts

**Symptoms:**
- Boots successfully
- No "Tick" messages appear
- System seems frozen but not crashed

**Causes:**

1. **Interrupts not enabled**
   ```bash
   # Check RFLAGS.IF
   register read rflags
   # Should have bit 9 set (0x200)
   ```

2. **IDT not set up**
   ```bash
   # Check IDT register
   register read idtr
   ```

3. **PIC not initialized**
   - Check idt::enable_irq(0) is called

**Debug:**
```lldb
breakpoint set --name timer_handler
continue
# If never hits, interrupts aren't firing
```

---

### Issue: Kernel Panic

**Symptoms:**
```
!!! KERNEL PANIC !!!
[message]
```

**Common panics:**

1. **"Stack overflow"**
   - Stack guard detected overflow
   - Increase stack size in boot64.S
   - Current: 32KB (should be enough)

2. **"Out of memory"**
   - PMM exhausted
   - Check PMM stats
   - May need to fix memory leak

---

## LLDB Commands Reference

### Connection
```lldb
# Terminal 1
qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic -s -S

# Terminal 2
lldb hft-zero.elf
gdb-remote localhost:1234
```

### Breakpoints
```lldb
# By function name
breakpoint set --name kernel_main
breakpoint set --name hft::pmm::allocate_page

# By address
breakpoint set --address 0xFFFFFFFF80111000

# List/delete
breakpoint list
breakpoint delete 1
```

### Execution Control
```lldb
continue    # Run until breakpoint
stepi       # Step one instruction
step        # Step one source line
next        # Step over function call
finish      # Run until function returns
```

### Memory Inspection
```lldb
# Read memory
x/10wx 0x103000           # 10 words (32-bit) in hex
x/20bx 0x103000           # 20 bytes in hex
x/s 0x102000              # Read as string

# Read registers
register read
register read rax rbx rcx
register write rax 0x1234
```

### Disassembly
```lldb
disassemble                # Current function
disassemble --name foo     # Specific function
disassemble --address 0x...  # At address
```

### Watchpoints
```lldb
# Watch memory location
watchpoint set expression -- 0x103000
watchpoint set variable g_some_var
```

---

## Useful Breakpoint Locations

```lldb
# Boot process
breakpoint set --address 0x101020  # _start
breakpoint set --address 0x101068  # check_cpuid
breakpoint set --address 0x101086  # check_long_mode
breakpoint set --address 0x1010d9  # setup_page_tables
breakpoint set --address 0x101132  # enable_paging
breakpoint set --address 0x101043  # lgdt
breakpoint set --address 0x10104a  # ljmp to 64-bit

# Kernel init
breakpoint set --name kernel_main
breakpoint set --name hft::idt::init
breakpoint set --name hft::pmm::init
breakpoint set --name hft::vmm::init

# Memory allocation
breakpoint set --name hft::pmm::allocate_page
breakpoint set --name hft::heap::allocate

# Interrupts
breakpoint set --name timer_handler
```

---

## Memory Inspection Helpers

### Check GDT
```lldb
# GDT entries (should see valid descriptors)
x/48bx 0x103000

# GDT pointer (should point to GDT)
x/10bx 0x103030
```

### Check Page Tables
```lldb
# PML4
x/512gx 0x104000

# Entry 0 (low mapping) and 511 (high mapping) should be present
```

### Check Kernel Code
```lldb
# Kernel entry point
x/20i 0xFFFFFFFF80111000

# Should see valid instructions, not zeros
```

### Check Stack
```lldb
# Current stack
register read rsp
x/32gx $rsp

# Stack should be in higher half (0xFFFFFFFF8...)
```

---

## Serial Output Debugging

### Add Debug Output
```cpp
// In any kernel code
serial::puts("DEBUG: Reached point X\n");
serial::put_hex(some_value);
serial::putc('\n');
```

### Common Debug Points
```cpp
// PMM init
serial::puts("PMM: Total pages = ");
serial::put_number(stats.total_pages);
serial::putc('\n');

// Memory allocation
serial::puts("Allocated page: ");
serial::put_hex(phys_addr);
serial::putc('\n');

// Page fault
serial::puts("Page fault at: ");
serial::put_hex(cr2);
serial::putc('\n');
```

---

## QEMU Monitor Commands

Press `Ctrl+A, C` to enter QEMU monitor:

```
info registers    # Show CPU registers
info mem          # Show page table mappings
info tlb          # Show TLB entries
x/10x 0x103000   # Read physical memory
system_reset      # Reboot
quit              # Exit QEMU
```

---

## Build Troubleshooting

### Compilation Errors

**"undefined reference to..."**
- Missing import statement
- Module not linked
- Check Makefile_phase1

**"error: ... has no member named..."**
- Wrong struct definition
- Check cppm module exports

### Linker Errors

**"section ... will not fit in region ..."**
- Kernel too large
- Check section sizes with `x86_64-elf-size`

**"undefined reference to `__kernel_start`"**
- Missing linker script symbols
- Check link.ld has __kernel_start defined

---

## Performance Profiling (Future)

### Measure Boot Time
```cpp
// At start of kernel_main
uint64_t start_tsc = __builtin_ia32_rdtsc();

// At end of init
uint64_t end_tsc = __builtin_ia32_rdtsc();
serial::put_number(end_tsc - start_tsc);
serial::puts(" cycles\n");
```

### Count Interrupts
```cpp
volatile uint64_t interrupt_count = 0;

void timer_handler(...) {
    interrupt_count++;
}
```

---

## Quick Sanity Checks

Run these commands to verify system state:

```bash
# 1. Check ELF is valid
x86_64-elf-readelf -h hft-zero.elf

# 2. Check entry point
x86_64-elf-readelf -h hft-zero.elf | grep Entry
# Should be 0x101020

# 3. Check segments are loadable
x86_64-elf-readelf -l hft-zero.elf | grep LOAD
# Should see 4-5 LOAD segments

# 4. Check kernel size
x86_64-elf-size hft-zero.elf
# text + data should be < 1MB

# 5. Verify symbols
x86_64-elf-nm hft-zero.elf | grep kernel_main
# Should show address in higher half

# 6. Check GDT data in binary
x86_64-elf-objdump -s -j .boot hft-zero.elf | tail -10
# Should see GDT entries at end
```

---

## Emergency Recovery

### If Nothing Works

1. **Start from scratch with minimal kernel:**
   ```cpp
   extern "C" void kernel_main() {
       while(1) asm("hlt");
   }
   ```

2. **Add features one at a time:**
   - Serial output
   - IDT
   - PMM
   - Timer
   - etc.

3. **Use known-good bootloader:**
   - GRUB2 instead of direct boot
   - Simplifies debugging

---

## Success Indicators

You know it's working when:

✅ Boots without reboot/triple fault
✅ Prints "K" immediately to serial
✅ Prints full banner
✅ All init steps complete
✅ Shows "System ready!"
✅ Timer interrupts fire (Tick messages)
✅ PMM shows >50,000 free pages
✅ No panics or crashes

---

## Getting Help

**Include in bug report:**
1. Full serial output
2. QEMU command used
3. Last successful breakpoint
4. Register dump at crash
5. Memory dump of relevant areas
6. Build log

**Useful info commands:**
```bash
uname -a                           # OS version
x86_64-elf-gcc --version          # Compiler version
qemu-system-x86_64 --version      # QEMU version
x86_64-elf-readelf -h hft-zero.elf # ELF info
```
