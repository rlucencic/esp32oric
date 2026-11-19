# Host build of the Oric core: make -C components/oric/host run
# (ROM= an absolute path or one relative to this folder)
ROOT      := ../../..
M6502     := ../../../../esp32emu/components/m6502
EMUDBG    := ../../../../esp32emu/components/emudbg
ORIC      := ..
BUILD     := build
CFLAGS    := -O2 -g -Wall -Wextra -Wno-unused-parameter -DM6502_HOST_HOOK -DORIC_HOST_HOOK -I$(ORIC)/include -I$(ORIC) \
             -I$(M6502)/include -I$(EMUDBG)/include
ROM       ?= $(ROOT)/roms/basic11b.rom
FRAMES    ?= 300

ORIC_SRCS := $(wildcard $(ORIC)/*.c)
ORIC_OBJS := $(patsubst $(ORIC)/%.c,$(BUILD)/oric/%.o,$(ORIC_SRCS)) $(BUILD)/m6502.o $(BUILD)/m6502_dbg.o \
             $(BUILD)/emudbg.o $(BUILD)/emudbg_posix.o

run: $(BUILD)/oric_host
	./$(BUILD)/oric_host $(ROM) $(FRAMES) $(BUILD)/screen.ppm

$(BUILD)/oric_host: host_main.c $(ORIC_OBJS)
	$(CC) $(CFLAGS) $^ -lm -lpthread -o $@

$(BUILD)/oric/%.o: $(ORIC)/%.c $(wildcard $(ORIC)/*.h $(ORIC)/include/*.h) $(M6502)/include/m6502.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(M6502)/%.c $(M6502)/include/m6502.h $(EMUDBG)/include/emudbg.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(EMUDBG)/%.c $(EMUDBG)/include/emudbg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Regression checks (the ROM needed): "Ready" and the bytes free, typed
# BASIC and its answer, typed capitals and small letters with CAPS off
check: $(BUILD)/oric_host
	ORIC_TEXT=1 ./$(BUILD)/oric_host $(ROM) 300 $(BUILD)/check.ppm | grep -q "37631 BYTES FREE"
	ORIC_TEXT=1 ./$(BUILD)/oric_host $(ROM) 300 $(BUILD)/check.ppm | grep -q "^|  Ready"
	ORIC_TEXT=1 ORIC_TYPE='150:PRINT 6*7:FOR I=1 TO 3:PRINT I;:NEXT\n' ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm \
	    | grep -q "^|   1  2  3"
	ORIC_TEXT=1 ORIC_TYPE="$$(printf '150:\024|200:REM Hello, World: [1+2]\\n')" ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm \
	    | grep -q "^|  REM Hello, World: \[1+2\]"
	@echo "host checks passed"

clean:
	rm -rf $(BUILD)

.PHONY: run check clean
