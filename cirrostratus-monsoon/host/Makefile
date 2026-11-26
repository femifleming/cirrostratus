# Host build: compiles the exact firmware DSP for the Mac and runs tests.
#   make -C host test      run all tests (AddressSanitizer + UBSan)
#   make -C host render    build the renderer

ROOT := ..
BUILD := build
CXX ?= clang++
CXXFLAGS := -std=c++14 -O2 -g -Wall -Wextra -Wno-unused-parameter \
            -Wno-missing-field-initializers -DTEST -I$(ROOT)
SAN := -fsanitize=address,undefined -fno-sanitize=pointer-overflow -fno-omit-frame-pointer
DSP_SRC := $(wildcard $(ROOT)/monsoon/dsp/*.cc) $(wildcard $(ROOT)/monsoon/dsp/modes/*.cc) $(BUILD)/cmsis_fft.o
CMSIS_INC := -I$(ROOT)/stmlib/third_party/STM/CMSIS/CM3_f4xx
TEST_SRC := $(wildcard tests_*.cc) test_main.cc
HDRS := $(wildcard $(ROOT)/monsoon/dsp/*.h) $(wildcard $(ROOT)/monsoon/dsp/modes/*.h) $(wildcard *.h)

all: $(BUILD)/tests $(BUILD)/render $(BUILD)/verify

$(BUILD)/tests: $(DSP_SRC) $(TEST_SRC) $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SAN) $(DSP_SRC) $(TEST_SRC) -o $@

$(BUILD)/render: $(DSP_SRC) render.cc $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DSP_SRC) render.cc -o $@

# The emulator verify run (tools/bench/verify.h) built for the Mac, to
# compare with the ARM build on QEMU (tools/bench/verify.sh).
$(BUILD)/verify: $(DSP_SRC) verify_main.cc $(ROOT)/tools/bench/verify.h $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DSP_SRC) verify_main.cc -o $@

$(BUILD):
	mkdir -p $(BUILD)

# CMSIS-DSP FFT, compiled as C (no sanitizers: third-party code).
$(BUILD)/cmsis_fft.o: $(ROOT)/monsoon/dsp/cmsis/cmsis_fft.c | $(BUILD)
	$(CC) -x c -O2 -w -DTEST -I$(ROOT) $(CMSIS_INC) -c $< -o $@

test: $(BUILD)/tests
	./$(BUILD)/tests $(T)

render: $(BUILD)/render

verify: $(BUILD)/verify

clean:
	rm -rf $(BUILD)

.PHONY: all test render verify clean

BOOT_SRC := $(ROOT)/stm_audio_bootloader/qpsk/demodulator.cc \
            $(ROOT)/stm_audio_bootloader/qpsk/packet_decoder.cc \
            $(ROOT)/stmlib/utils/random.cc
$(BUILD)/bootloader_sim: bootloader_sim.cc $(BOOT_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Wno-sign-compare -Wno-unused-variable $(BOOT_SRC) bootloader_sim.cc -o $@
