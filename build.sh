#!/bin/bash

# HFT-Zero Phase 1 Build Script

set -e

echo "================================================"
echo "       HFT-Zero Phase 1 Build Script"
echo "================================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for required tools
check_tool() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}Error: $1 not found${NC}"
        echo "Please install: $2"
        exit 1
    fi
}

echo "Checking build environment..."
check_tool x86_64-elf-gcc "x86_64-elf cross-compiler toolchain"
check_tool x86_64-elf-g++ "x86_64-elf cross-compiler toolchain"
check_tool x86_64-elf-as "x86_64-elf cross-compiler toolchain"
check_tool x86_64-elf-ld "x86_64-elf cross-compiler toolchain"
check_tool qemu-system-x86_64 "QEMU x86_64 emulator"
check_tool make "GNU Make"

# Check GCC version
GCC_VERSION=$(x86_64-elf-g++ --version | head -n1)
echo -e "${GREEN}Found: $GCC_VERSION${NC}"
echo ""

# Parse command line arguments
BUILD_TYPE="release"
RUN_AFTER_BUILD=false
DEBUG_MODE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="debug"
            shift
            ;;
        --run)
            RUN_AFTER_BUILD=true
            shift
            ;;
        --gdb)
            DEBUG_MODE=true
            shift
            ;;
        --clean)
            echo "Cleaning build files..."
            make -f Makefile_phase1 clean
            echo -e "${GREEN}Clean complete!${NC}"
            exit 0
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --debug    Build with debug symbols"
            echo "  --run      Run in QEMU after building"
            echo "  --gdb      Start GDB debugging session"
            echo "  --clean    Clean build files"
            echo "  --help     Show this help message"
            echo ""
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Build the kernel
echo "================================================"
echo "Building HFT-Zero Phase 1 ($BUILD_TYPE mode)..."
echo "================================================"
echo ""

if [ "$BUILD_TYPE" = "debug" ]; then
    export CXXFLAGS="-O0 -g3 -DDEBUG"
else
    export CXXFLAGS="-O2 -DNDEBUG"
fi

# Run make with progress
make -f Makefile_phase1 -j$(nproc) || {
    echo -e "${RED}Build failed!${NC}"
    exit 1
}

echo ""
echo -e "${GREEN}Build successful!${NC}"
echo ""

# Check if kernel was created
if [ ! -f "hft-zero.elf" ]; then
    echo -e "${RED}Error: Kernel binary not found!${NC}"
    exit 1
fi

# Display kernel info
echo "Kernel Information:"
echo "-------------------"
size hft-zero.elf | tail -n1
echo ""

# Run if requested
if [ "$RUN_AFTER_BUILD" = true ]; then
    echo "================================================"
    echo "Starting QEMU..."
    echo "================================================"
    echo ""
    echo "Press Ctrl+A then X to exit QEMU"
    echo ""
    sleep 2
    make -f Makefile_phase1 run
elif [ "$DEBUG_MODE" = true ]; then
    echo "================================================"
    echo "Starting GDB Debug Session..."
    echo "================================================"
    echo ""
    echo "GDB will connect to QEMU on port 1234"
    echo "Use 'continue' to start execution"
    echo ""
    make -f Makefile_phase1 debug
else
    echo "Kernel built successfully!"
    echo ""
    echo "To run:   make -f Makefile_phase1 run"
    echo "To debug: make -f Makefile_phase1 debug"
    echo ""
    echo "Or use this script:"
    echo "  $0 --run    # Build and run"
    echo "  $0 --gdb    # Build and debug"
fi
