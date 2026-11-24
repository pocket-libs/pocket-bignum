CC      ?= cc
GMP     ?= /usr/local
CFLAGS  ?= -O2 -g -std=c99 -Wall -Wextra -Wno-unused-function
GMPINC   = -I$(GMP)/include
GMPLIB   = -L$(GMP)/lib -lgmp
MPFRLIB  = -L$(GMP)/lib -lmpfr -lgmp
SAN      = -fsanitize=address,undefined -fno-omit-frame-pointer
ITERS   ?= 20000
SEED    ?= 1

# tiny thresholds: Karatsuba, Toom-3 & divide-and-conquer radix conversion on almost every operation
SMALLTHR = -DPBN_MUL_KARA_THRESHOLD=2 -DPBN_SQR_KARA_THRESHOLD=2 -DPBN_MUL_TOOM3_THRESHOLD=5 -DPBN_SQR_TOOM3_THRESHOLD=5 \
           -DPBN_MUL_TOOM4_THRESHOLD=13 -DPBN_SQR_TOOM4_THRESHOLD=13 \
           -DPBN_GET_STR_DC_THRESHOLD=2 -DPBN_SET_STR_DC_THRESHOLD=1 -DPBN_DC_DIV_THRESHOLD=2 -DPBN_FAC_SWING_THRESHOLD=2

BUILD = build

.PHONY: all test test64 test32 testportable testqf testcompat testthresholds arturo-check bench bench-ruby bench-small clean

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

$(BUILD)/fuzzthr: tests/fuzz.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(SMALLTHR) $(GMPINC) $< -o $@ $(GMPLIB) -lm

$(BUILD)/fuzzqfthr: tests/fuzz_qf.c pocketbignum.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $(SMALLTHR) $(GMPINC) $< -o $@ $(MPFRLIB) -lm

$(BUILD)/bench_ops: tests/bench_ops.c pocketbignum.h | $(BUILD)
	$(CC) -O3 -std=c99 -DNDEBUG $< -o $@ -lm

$(BUILD)/bench_small: tests/bench_small.c pocketbignum.h | $(BUILD)
	$(CC) -O3 -std=c99 -DNDEBUG $< -o $@ -lm

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

testthresholds: $(BUILD)/fuzzthr $(BUILD)/fuzzqfthr
	./$(BUILD)/fuzzthr $(ITERS) $(SEED)
	./$(BUILD)/fuzzqfthr $(ITERS) $(SEED)

testqf: $(BUILD)/fuzzqf64 $(BUILD)/fuzzqf32 $(BUILD)/fuzzqfportable
	./$(BUILD)/fuzzqf64 $(ITERS) $(SEED)
	./$(BUILD)/fuzzqf32 $(ITERS) $(SEED)
	./$(BUILD)/fuzzqfportable $(ITERS) $(SEED)

test: test64 test32 testportable testqf testcompat testthresholds

arturo-check:
	./tools/check_arturo_surface.sh $(ARTURO)

bench: $(BUILD)/bench $(BUILD)/bench_qf
	./$(BUILD)/bench
	./$(BUILD)/bench_qf

# small numbers, the way Arturo uses them
bench-small: $(BUILD)/bench_small
	./$(BUILD)/bench_small

# PocketBignum vs Ruby's Integer (mul, sqr, sqrt, get_str, set_str at large sizes)
bench-ruby: $(BUILD)/bench_ops
	./$(BUILD)/bench_ops > $(BUILD)/ops-pbn.txt
	ruby tests/bench_ops.rb > $(BUILD)/ops-ruby.txt
	python3 tools/compare_ops.py $(BUILD)/ops-pbn.txt $(BUILD)/ops-ruby.txt

clean:
	rm -rf $(BUILD)
