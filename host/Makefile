# Host build: compiles the exact firmware DSP for the Mac and runs tests.
#   make -C host test      run all tests (AddressSanitizer + UBSan)
#   make -C host render    build the renderer

ROOT := ..
BUILD := build
CXX ?= clang++
CXXFLAGS := -std=c++14 -O2 -g -Wall -Wextra -Wno-unused-parameter \
            -Wno-missing-field-initializers -DTEST -I$(ROOT)
SAN := -fsanitize=address,undefined -fno-sanitize=pointer-overflow -fno-omit-frame-pointer
DSP_SRC := $(wildcard $(ROOT)/monsoon/dsp/*.cc) $(wildcard $(ROOT)/monsoon/dsp/modes/*.cc)
TEST_SRC := $(wildcard tests_*.cc) test_main.cc
HDRS := $(wildcard $(ROOT)/monsoon/dsp/*.h) $(wildcard $(ROOT)/monsoon/dsp/modes/*.h) $(wildcard *.h)

all: $(BUILD)/tests $(BUILD)/render

$(BUILD)/tests: $(DSP_SRC) $(TEST_SRC) $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SAN) $(DSP_SRC) $(TEST_SRC) -o $@

$(BUILD)/render: $(DSP_SRC) render.cc $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(DSP_SRC) render.cc -o $@

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/tests
	./$(BUILD)/tests $(T)

render: $(BUILD)/render

clean:
	rm -rf $(BUILD)

.PHONY: all test render clean

BOOT_SRC := $(ROOT)/stm_audio_bootloader/qpsk/demodulator.cc \
            $(ROOT)/stm_audio_bootloader/qpsk/packet_decoder.cc \
            $(ROOT)/stmlib/utils/random.cc
$(BUILD)/bootloader_sim: bootloader_sim.cc $(BOOT_SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Wno-sign-compare -Wno-unused-variable $(BOOT_SRC) bootloader_sim.cc -o $@
