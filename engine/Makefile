CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pedantic -Werror -Iinclude
SRC = src/chunk.cpp src/disassembler.cpp src/verifier.cpp src/vm.cpp src/closure.cpp src/exceptions.cpp src/debug.cpp src/object.cpp src/allocator.cpp src/ffi.cpp src/root_set.cpp src/safepoint.cpp src/gc_mark_sweep.cpp src/incremental_gc.cpp src/card_table.cpp src/generational.cpp src/handles.cpp src/compact.cpp src/weak_ref.cpp src/finalizer.cpp src/gc_telemetry.cpp
TEST_SRC = tests/test_runner.cpp
BIN = bin/test_runner

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(SRC) $(TEST_SRC)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) $(TEST_SRC) -o $(BIN)

run: $(BIN)
	./$(BIN)

test: $(BIN)
	./$(BIN)

clean:
	rm -rf bin
