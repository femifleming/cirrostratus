# Host tests for the fletchfoorth engine: make -f stages/test/dobbel.mk test
CXX ?= c++
CXXFLAGS = -std=c++11 -O2 -g -Wall -Werror -Wno-unused-local-typedefs -I. \
	-fsanitize=address,undefined -DTEST
SRC = stages/test/dobbel_test.cc stages/dobbel.cc stages/dobbel_bank.cc stages/resources.cc stmlib/utils/random.cc
BIN = build/dobbel_test/dobbel_test

$(BIN): $(SRC) stages/dobbel.h
	mkdir -p build/dobbel_test
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

test: $(BIN)
	$(BIN)

render: $(BIN)
	mkdir -p build/dobbel_test/renders
	$(BIN) build/dobbel_test/renders

.PHONY: test render
