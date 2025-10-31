CXX = x86_64-elf-g++
AS = x86_64-elf-as
LD = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

CXXFLAGS = -std=c++26 -O3 -ffreestanding -fno-exceptions -fno-rtti \
           -mno-red-zone -mcmodel=kernel -march=native -mtune=native \
           -Wall -Wextra -Wpedantic -fno-stack-protector -fno-pic \
           -Iinclude -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
           -fno-omit-frame-pointer

LDFLAGS = -T link.ld -nostdlib -z max-page-size=0x1000

SRCS = boot/boot.S \
       kernel/main.cpp

OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.S=.o)

TARGET = hft-zero.elf

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	$(OBJCOPY) -O binary $@ hft-zero.bin

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.S
	$(AS) -o $@ $<

run: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -m 4G -enable-kvm

debug: $(TARGET)
	qemu-system-x86_64 -kernel $(TARGET) -m 4G -s -S &
	gdb $(TARGET) -ex "target remote :1234"

clean:
	rm -f $(OBJS) $(TARGET) hft-zero.bin

.PHONY: all run debug clean
