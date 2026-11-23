CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS  = -lexpat -lm

BUILD   = build
TARGET  = gpx2fit

# data/default.map is embedded into the binary as a C string (task 5.3).
# Needs xxd (ships with vim). The header is regenerated when the map changes.
GEN_HDR = src/default_map.h

SRC     = $(filter-out $(wildcard src/test_*.c),$(wildcard src/*.c))
OBJ     = $(SRC:src/%.c=$(BUILD)/%.o)
DEP     = $(OBJ:.o=.d)

.PHONY: all test clean asan test-asan

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Only symmap.c includes the generated header. (After the first build the
# -MMD dependency files cover this too; this line covers the very first one.)
$(BUILD)/symmap.o: $(GEN_HDR)

$(GEN_HDR): data/default.map
	{ printf '/* generated from %s by make; do not edit */\n' '$<' && \
	  printf 'static const char default_map_text[] = {\n' && \
	  xxd -i < '$<' && \
	  printf ', 0x00\n};\n'; } > $@.tmp
	mv $@.tmp $@

$(BUILD):
	mkdir -p $@

# Sanitizer build (ASan + UBSan) into a separate binary and object dir.
asan:
	$(MAKE) all BUILD=build-asan TARGET=gpx2fit-asan \
	    CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer" \
	    LDFLAGS="-fsanitize=address,undefined"

# Tests: every tests/test_*.c is built against all of src/ except main.c,
# with ASan + UBSan (any report aborts the test), then run.
# Then every tests/test_*.sh and the end-to-end tests/run.sh run against the
# built binary.
LIB_SRC   = $(filter-out src/main.c,$(SRC))
TEST_SRC  = $(wildcard tests/test_*.c)
TEST_BIN  = $(TEST_SRC:tests/%.c=build-test/%)
TEST_SH   = $(wildcard tests/test_*.sh) tests/run.sh
SAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined \
            -fno-omit-frame-pointer

build-test/%: tests/%.c $(LIB_SRC) $(wildcard src/*.h) $(GEN_HDR) | build-test
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic $(SAN_FLAGS) -Isrc \
	    -o $@ $< $(LIB_SRC) $(LDLIBS)

build-test:
	mkdir -p $@

test: all $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; ./$$t; done
	@set -e; for s in $(TEST_SH); do echo "== $$s"; bash $$s ./$(TARGET); done

# Shell / end-to-end tests against the ASan+UBSan binary (task 10.1).
# Any sanitizer report aborts the binary, which the tests see as a failure.
# Not part of `make test` (the unit tests there already use sanitizers).
test-asan: asan
	@set -e; for s in $(TEST_SH); do echo "== $$s (asan)"; \
	    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	    bash $$s ./$(TARGET)-asan; done

clean:
	rm -rf $(BUILD) build-asan build-test $(TARGET) $(TARGET)-asan $(GEN_HDR) $(GEN_HDR).tmp

-include $(DEP)
