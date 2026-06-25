###############################################################################
# Makefile - MDM Flight Software Build
#
# Target:  i386-pc-linux-gnu (cross-compile for 80386EX in production)
# Host:    Linux (development)
# CC:      gcc
# CFLAGS:  -O2 -Wall -Wextra -pedantic -std=c89
#
# This Makefile uses BSD-style syntax and old-school suffix rules.
# It requires GNU Make.
#
# Targets:
#   all       - Build the MDM executable
#   clean     - Remove object files and executable
#   test      - Run built-in self-test
#   bridge    - Build and start with Python NATS bridge
###############################################################################

CC       = gcc
LD       = gcc

CFLAGS  = -O2 -Wall -Wextra -std=c89
CFLAGS  += -D_POSIX_C_SOURCE=199309L
CFLAGS  += -Werror=implicit-function-declaration
CFLAGS  += -Werror=return-type
CFLAGS  += -Wno-unused-parameter
CFLAGS  += -DMDM_TARGET_LINUX

LDFLAGS  = -lm

SRCS     = main.c mdm_core.c bus_1553.c cmd_handler.c tlm_pack.c
OBJS     = main.o mdm_core.o bus_1553.o cmd_handler.o tlm_pack.o
HDRS     = mdm.h
TARGET   = mdm

###############################################################################
# Suffix rules (old school)
###############################################################################

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

###############################################################################
# Targets
###############################################################################

.PHONY: all clean test bridge

all: $(TARGET)

$(TARGET): $(OBJS) $(HDRS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "============================================"
	@echo " MDM build complete: $(TARGET)"
	@echo " Run: ./$(TARGET) <mdm_id> <bus_addr>"
	@echo " Example: ./$(TARGET) 1 0x12"
	@echo "============================================"

$(OBJS): $(HDRS)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f *.o *.a *.so
	@echo "Clean done."

test: $(TARGET)
	@echo "Running MDM self-test..."
	@echo "1" | ./$(TARGET) 1 0x12 &
	@sleep 1
	@kill %1 2>/dev/null || true
	@echo "Test complete."

bridge: $(TARGET)
	@echo "Starting MDM with NATS bridge..."
	./$(TARGET) 1 0x12 | python3 bridge.py

###############################################################################
# Cross-compilation for 80386EX target (requires cross-compiler)
###############################################################################

CC_386      = i386-elf-gcc
LD_386      = i386-elf-ld
CFLAGS_386  = -O2 -Wall -Wextra -std=c89 -march=i386 -ffreestanding -nostdlib
TARGET_386  = mdm.bin

386: CFLAGS = $(CFLAGS_386)
386: $(OBJS)
	$(LD_386) -Ttext 0x10000 -o $(TARGET_386) $(OBJS)
	@echo "Cross-compile complete: $(TARGET_386)"

###############################################################################
# End of Makefile
###############################################################################
