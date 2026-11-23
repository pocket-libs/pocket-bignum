CC      ?= cc
GMP     ?= /usr/local
CFLAGS  ?= -O2 -g -std=c99 -Wall -Wextra -Wno-unused-function
GMPINC   = -I$(GMP)/include
GMPLIB   = -L$(GMP)/lib -lgmp
MPFRLIB  = -L$(GMP)/lib -lmpfr -lgmp
SAN      = -fsanitize=address,undefined -fno-omit-frame-pointer
ITERS   ?= 20000
SEED    ?= 1

BUILD = build

.PHONY: all test test64 test32 testportable testqf testcompat arturo-check bench clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/fuzz64: tests/fuzz.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(GMPINC) $< -o $@ $(GMPLIB) -lm

$(BUILD)/fuzz32: tests/fuzz.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -DPBN_LIMB_BITS=32 $(GMPINC) $< -o $@ $(GMPLIB) -lm

$(BUILD)/fuzzportable: tests/fuzz.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -DPBN_PORTABLE $(GMPINC) $< -o $@ $(GMPLIB) -lm

$(BUILD)/fuzzqf64: tests/fuzz_qf.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(GMPINC) $< -o $@ $(MPFRLIB) -lm

$(BUILD)/fuzzqf32: tests/fuzz_qf.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -DPBN_LIMB_BITS=32 $(GMPINC) $< -o $@ $(MPFRLIB) -lm

$(BUILD)/fuzzqfportable: tests/fuzz_qf.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -DPBN_PORTABLE $(GMPINC) $< -o $@ $(MPFRLIB) -lm

$(BUILD)/bench: tests/bench.c pocketbignum.h | $(BUILD)
	$(CC) -O3 -std=c99 -DNDEBUG $(GMPINC) $< -o $@ $(GMPLIB) -lm

$(BUILD)/bench_qf: tests/bench_qf.c pocketbignum.h | $(BUILD)
	$(CC) -O3 -std=c99 -DNDEBUG $(GMPINC) $< -o $@ $(MPFRLIB) -lm

$(BUILD)/compat: tests/compat_smoke.c pocketbignum.h compat/gmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Icompat $< -o $@ -lm

test64: $(BUILD)/fuzz64
	./$(BUILD)/fuzz64 $(ITERS) $(SEED)

test32: $(BUILD)/fuzz32
	./$(BUILD)/fuzz32 $(ITERS) $(SEED)

testportable: $(BUILD)/fuzzportable
	./$(BUILD)/fuzzportable $(ITERS) $(SEED)

testcompat: $(BUILD)/compat
	./$(BUILD)/compat

testqf: $(BUILD)/fuzzqf64 $(BUILD)/fuzzqf32 $(BUILD)/fuzzqfportable
	./$(BUILD)/fuzzqf64 $(ITERS) $(SEED)
	./$(BUILD)/fuzzqf32 $(ITERS) $(SEED)
	./$(BUILD)/fuzzqfportable $(ITERS) $(SEED)

test: test64 test32 testportable testqf testcompat

arturo-check:
	./tools/check_arturo_surface.sh $(ARTURO)

bench: $(BUILD)/bench $(BUILD)/bench_qf
	./$(BUILD)/bench
	./$(BUILD)/bench_qf

clean:
	rm -rf $(BUILD)
