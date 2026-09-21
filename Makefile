# Builds the benchmarks and the copy of burrow they run against.
#
# By default it fetches burrow's main branch into build/burrow, so a clone of
# this repo on its own is enough to get numbers. Point BURROW_DIR at a working
# copy when you are measuring a change you have not pushed:
#
#     make BURROW_DIR=../burrow bench

CC        ?= cc
BUILD     ?= build
BURROW_DIR ?= $(BUILD)/burrow

# Passed straight through to the runner, so `make bench BENCHFLAGS='-run str'`
# works and so does -tsv.
BENCHFLAGS ?=

STD      := -std=c11
INCLUDES := -Iinclude -I$(BURROW_DIR)/include

# Optimisation is not optional here. A benchmark built at -O0 measures the
# compiler's debug output and tells you nothing about the library. -O2 and not
# -O3 because -O2 is what people actually ship with.
OPT := -O2 -g

# A much shorter warning set than burrow's, on purpose. This is throwaway
# measurement code, and -Wconversion in particular turns every int64 loop
# counter into a cast that clutters the thing being measured.
WARNINGS := -Wall -Wextra -Werror -Wno-unused-parameter

# -fno-omit-frame-pointer so that perf and Instruments can walk the stack when
# a number looks wrong and somebody wants to know why.
CFLAGS  ?= $(STD) $(OPT) $(WARNINGS) -fno-omit-frame-pointer $(INCLUDES)
LDFLAGS ?=

# burrow itself needs threads now, so anything linking it does too. -pthread is
# a compile flag and a link flag at once, which is why it goes on both, and it
# is kept out of CFLAGS because CFLAGS is overridable and this is not optional.
# Windows has threads in the CRT with no flag to ask for them.
ifneq ($(OS),Windows_NT)
  THREADS := -pthread
endif

# Sanitisers, for checking that a benchmark is measuring what it says rather
# than running off the end of a buffer. Set SAN and both sides get rebuilt with
# it, which matters because MemorySanitizer reports anything it did not watch
# being written, so a sanitised benchmark linked against an unsanitised burrow
# is nothing but false positives.
SAN ?=
BURROW_MAKE := CC=$(CC)
ifneq ($(SAN),)
  CFLAGS += $(SAN)
  LDFLAGS += $(SAN)
  BURROW_MAKE += MODE=debug CFLAGS='-std=c11 -O1 -g -Iinclude -DBURROW_SOURCE_ID="bench" $(SAN)'
endif

# Where burrow's own objects land, which is a directory of ours inside its tree
# rather than its default one. Two reasons, and the first one cost an afternoon.
#
# make passes a variable set on its command line down to every sub-make, so
# `make BUILD=build-eight` moved burrow's library to build-eight/libburrow.a
# while the link line below still asked for build/libburrow.a, and since a
# library was sitting there from an earlier run the link succeeded against a
# burrow built from different headers. That is not a stale build, it is two
# different definitions of the same struct in one binary, and what it produced
# was benchmark numbers with nothing wrong with them on the face of it.
#
# The second is the sanitiser. A sanitised burrow and a plain one cannot share
# object files, and nothing in a timestamp says which one is in there.
#
# burrow ignores build*/, so this leaves no mark on a working copy somebody
# pointed BURROW_DIR at.
BURROW_BUILD := build-bench$(if $(SAN),-san)
BURROW_MAKE  += BUILD=$(BURROW_BUILD)
BURROW_LIB   := $(BURROW_DIR)/$(BURROW_BUILD)/libburrow.a
BENCH_SRCS  := $(wildcard bench/*_bench.c)
HARNESS     := src/bench.c src/main.c
BIN         := $(BUILD)/bench

DEPFLAGS := -MMD -MP
DEPS     := $(BIN).d

.PHONY: all bench list burrow clean fmt go

all: $(BIN)

# The fetch script is a no-op when BURROW_DIR is a working copy you pointed at,
# which is how the same rule serves both cases.
burrow:
	@BURROW_DIR=$(BURROW_DIR) tools/fetch-burrow.sh

$(BURROW_LIB): burrow
	$(MAKE) -C $(BURROW_DIR) $(BURROW_MAKE) lib

$(BIN): $(HARNESS) $(BENCH_SRCS) $(BURROW_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(THREADS) $(DEPFLAGS) -MF $@.d $(HARNESS) $(BENCH_SRCS) $(BURROW_LIB) $(LDFLAGS) -o $@

-include $(DEPS)

bench: $(BIN)
	@$(BIN) $(BENCHFLAGS)

list: $(BIN)
	@$(BIN) -list

# The Go side, for a machine that has a Go toolchain. Not a dependency of
# anything, because most machines running this will not have one.
go:
	cd go && go test -bench . -benchmem ./...

fmt:
	clang-format -i $(shell git ls-files --cached --others --exclude-standard '*.c' '*.h')

clean:
	rm -rf $(BUILD)
