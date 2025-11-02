# HFT-Zero Quick Start Guide

## Overview

This guide will get you up and running with HFT-Zero Phase 1 in under 5 minutes.

## Prerequisites Check

```bash
# Verify cross-compiler
x86_64-elf-gcc --version   # Should be 15.2+
x86_64-elf-g++ --version

# Verify QEMU
qemu-system-x86_64 --version

# Verify build tools
make --version
```

If any tools are missing, see the full README.md for installation instructions.

## Quick Build

### Option 1: Using Build Script

```bash
chmod +x build.sh
./build.sh --run
```

### Option 2: Manual Build

```bash
# Clean previous build
make -f Makefile_phase1 clean

# Build kernel
make -f Makefile_phase1

# Run in QEMU
qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic
```

## Expected Output

When successful, you'll see:

```
K
=====================================
        HFT-Zero Kernel v0.1        
=====================================

[*] Initializing IDT... [OK]
[*] Initializing PMM... 
    Kernel: 0x0000000000110000 - 0x000000000011bdb8
    Using fallback (256MB)
    Free pages: 61440 / 65536
[OK]
[*] Initializing heap... [OK]
[*] Initializing timer... [OK]
[*] Enabling interrupts... [OK]
[*] Initializing kernel... [OK]

System ready!

Tick 1
Tick 2
...
```

Press `Ctrl+A` then `X` to exit QEMU.

## What Just Happened

1. **Boot Sequence**
   - CPU transitioned from 32-bit to 64-bit long mode
   - Page tables set up for higher-half kernel
   - GDT and IDT initialized

2. **Memory Detection**
   - Fallback initialization used (QEMU doesn't pass multiboot info)
   - 256MB total RAM configured
   - Physical memory manager initialized with 240MB usable

3. **System Initialization**
   - Heap allocator ready
   - Timer interrupts configured (100Hz)
   - All subsystems operational

## Key Features Working

- 64-bit long mode execution
- Higher-half kernel at 0xFFFFFFFF80000000
- Physical memory allocation
- Heap allocation (kmalloc/kfree)
- Timer interrupts (PIT at 100Hz)
- Serial console debugging

## File Sizes

```bash
# Check kernel size
ls -lh hft-zero.elf    # ~70KB
ls -lh hft-zero.bin    # ~20KB
```

## Debugging

### Quick Debug Session

```bash
# Terminal 1: Start QEMU with GDB stub
qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic -s -S

# Terminal 2: Connect LLDB
lldb hft-zero.elf
(lldb) gdb-remote localhost:1234
(lldb) breakpoint set --name kernel_main
(lldb) continue
```

### Common Breakpoints

```lldb
# Boot sequence
breakpoint set --address 0x101020  # _start
breakpoint set --name kernel_main

# Memory management
breakpoint set --name hft::pmm::allocate_page
breakpoint set --name hft::heap::allocate

# Interrupts
breakpoint set --name timer_handler
```

## Verification

### Success Indicators

1. Boot completes without triple fault
2. Serial output shows "System ready!"
3. PMM reports 61,440 free pages (240MB)
4. Timer ticks appear (Tick 1, Tick 2, etc.)
5. No kernel panics

### Quick Health Check

```bash
# After boot, in QEMU monitor (Ctrl+A, C)
info registers    # Should show RIP in kernel space
info mem         # Should show mapped pages
```

## Common Issues

### Issue: Triple Fault

**Symptom:** QEMU reboots immediately, never see output

**Solution:**
- Verify boot64.S has all fixes applied
- Check linker script (link.ld) is correct
- Ensure all boot sections are in .boot output section

### Issue: Zero Free Pages

**Symptom:** PMM shows "Free pages: 0 / 65536"

**Solution:**
- Verify main_phase1.cpp parses multiboot info
- Check multiboot2 magic (0x36d76289)
- Ensure fallback initialization works

### Issue: No Timer Interrupts

**Symptom:** Boot succeeds but no "Tick" messages

**Solution:**
- Verify IDT is initialized
- Check interrupts are enabled (sti)
- Ensure timer_handler is registered

## Next Steps

### Explore the System

```cpp
// Add to kernel/main_phase1.cpp after "System ready!"

// Test memory allocation
serial::puts("Testing memory allocation...\n");
uint64_t page = pmm::allocate_page();
serial::puts("Allocated page at: ");
serial::put_hex(page);
serial::putc('\n');

// Test heap
serial::puts("Testing heap...\n");
void* ptr = heap::allocate(1024);
serial::puts("Heap allocation: ");
serial::put_hex(reinterpret_cast<uint64_t>(ptr));
serial::putc('\n');
```

### Read the Documentation

- `docs/ARCHITECTURE.md` - Deep dive into system design
- `docs/ROADMAP.md` - Development plan
- `docs/DEBUGGING.md` - Debugging techniques
- `README.md` - Comprehensive overview

### Modify and Experiment

1. **Change timer frequency:**
   ```cpp
   // In init_timer(), change divisor
   uint32_t divisor = 1193180 / 1000;  // 1000Hz instead of 100Hz
   ```

2. **Add debug output:**
   ```cpp
   serial::puts("DEBUG: Your message here\n");
   serial::put_hex(some_value);
   ```

3. **Test memory allocation:**
   ```cpp
   for (int i = 0; i < 100; i++) {
       uint64_t page = pmm::allocate_page();
       // Use page
   }
   ```

## Build Optimization

### Debug Build

```bash
# Build with debug symbols
export CXXFLAGS="-O0 -g3 -DDEBUG"
make -f Makefile_phase1
```

### Release Build

```bash
# Optimized build
export CXXFLAGS="-O2 -DNDEBUG"
make -f Makefile_phase1
```

## Performance Testing

### Boot Time

```cpp
// In kernel_main, start of function
uint64_t start_tsc = __builtin_ia32_rdtsc();

// After "System ready!"
uint64_t end_tsc = __builtin_ia32_rdtsc();
serial::puts("Boot cycles: ");
serial::put_number((end_tsc - start_tsc) / 1000000);  // Approx ms
serial::puts("M\n");
```

### Memory Allocation Speed

```cpp
uint64_t start = __builtin_ia32_rdtsc();
for (int i = 0; i < 1000; i++) {
    uint64_t page = pmm::allocate_page();
    pmm::free_page(page);
}
uint64_t end = __builtin_ia32_rdtsc();
serial::puts("Avg cycles per alloc/free: ");
serial::put_number((end - start) / 2000);
serial::putc('\n');
```

## Development Workflow

```bash
# 1. Make changes to source
vim kernel/main_phase1.cpp

# 2. Clean build
make -f Makefile_phase1 clean

# 3. Build
make -f Makefile_phase1

# 4. Test
qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic

# 5. Debug if needed
# Terminal 1:
qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic -s -S
# Terminal 2:
lldb hft-zero.elf
gdb-remote localhost:1234
```

## Cleaning Up

```bash
# Remove build artifacts
make -f Makefile_phase1 clean

# Also removes GCM cache
rm -rf gcm.cache/

# Full clean
make -f Makefile_phase1 clean
rm -f hft-zero.elf hft-zero.bin hft-zero.iso
rm -rf isodir/
```

## Getting Help

### Check Serial Output

All kernel messages go to serial port. In QEMU, they appear in the terminal.

### Check Registers

```lldb
(lldb) register read
(lldb) register read rip rsp
```

### Memory Dump

```lldb
# Dump kernel memory
x/100gx 0xFFFFFFFF80111000

# Dump page tables
x/512gx 0x104000  # PML4
```

### QEMU Monitor

Press `Ctrl+A, C` for QEMU monitor:
```
info registers
info mem
info tlb
x/10x 0x111000
```

## Summary

You now have:
- A working 64-bit kernel
- Memory management (physical and virtual)
- Interrupt handling
- Timer ticks
- Heap allocation
- Debugging tools

Phase 1 is complete. Proceed to Phase 2 for network stack and advanced features.

---

**Quick Reference:**

Build: `make -f Makefile_phase1`
Run: `qemu-system-x86_64 -kernel hft-zero.elf -m 256M -nographic`
Debug: `qemu ... -s -S` then `lldb hft-zero.elf`
Exit QEMU: `Ctrl+A, X`
