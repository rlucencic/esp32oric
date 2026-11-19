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
# BASIC and its answer, typed capitals and small letters with CAPS off;
# CSAVE onto a blank tape (its header), CLOAD by name fast and at 300 baud
# in real time; a loader of its own (tools/loadertap.py) fast and in real
# time
check: $(BUILD)/oric_host
	ORIC_TEXT=1 ./$(BUILD)/oric_host $(ROM) 300 $(BUILD)/check.ppm | grep -q "37631 BYTES FREE"
	ORIC_TEXT=1 ./$(BUILD)/oric_host $(ROM) 300 $(BUILD)/check.ppm | grep -q "^|  Ready"
	ORIC_TEXT=1 ORIC_TYPE='150:PRINT 6*7:FOR I=1 TO 3:PRINT I;:NEXT\n' ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm \
	    | grep -q "^|   1  2  3"
	ORIC_TEXT=1 ORIC_TYPE="$$(printf '150:\024|200:REM Hello, World: [1+2]\\n')" ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm \
	    | grep -q "^|  REM Hello, World: \[1+2\]"
	rm -f $(BUILD)/check.tap
	ORIC_NEWTAPE=$(BUILD)/check.tap ORIC_TYPE='150:10 PRINT "TAPE"\nCSAVE"ONE"\nCSAVE"TWO",S\n' \
	    ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm > /dev/null
	head -c 13 $(BUILD)/check.tap | od -An -tx1 | grep -q "16 16 16 24 00 00 00 00 05 10 05 01 00"
	ORIC_TAPE=$(BUILD)/check.tap ORIC_TEXT=1 ORIC_TYPE='150:CLOAD"TWO"\n|300:LIST\n' \
	    ./$(BUILD)/oric_host $(ROM) 500 $(BUILD)/check.ppm | grep -q '^|   10 PRINT "TAPE"'
	ORIC_REALTIME=1 ORIC_TAPE=$(BUILD)/check.tap ORIC_TEXT=1 ORIC_TYPE='150:CLOAD"TWO",S\n|1000:LIST\n' \
	    ./$(BUILD)/oric_host $(ROM) 1200 $(BUILD)/check.ppm | grep -q '^|   10 PRINT "TAPE"'
	python3 -I $(ROOT)/tools/loadertap.py $(ROM) $(BUILD)/loader.tap > /dev/null
	cp $(BUILD)/loader.tap $(BUILD)/loader2.tap
	ORIC_TAPE=$(BUILD)/loader2.tap ORIC_DUMP=0800:30 ORIC_TYPE='150:CLOAD""\n' \
	    ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm | grep -q "^00 00 80 00 50 0F 50 00 00 44 41 54 41 00 30 31"
	cp $(BUILD)/loader.tap $(BUILD)/loader2.tap
	ORIC_REALTIME=1 ORIC_TAPE=$(BUILD)/loader2.tap ORIC_DUMP=0800:30 ORIC_TYPE='150:CLOAD""\n' \
	    ./$(BUILD)/oric_host $(ROM) 600 $(BUILD)/check.ppm | grep -q "^00 00 80 00 50 0F 50 00 00 44 41 54 41 00 30 31"
	@echo "host checks passed"

clean:
	rm -rf $(BUILD)

.PHONY: run check clean
