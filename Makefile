CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS  = -lexpat -lm

BUILD   = build
TARGET  = gpx2fit

SRC     = $(filter-out $(wildcard src/test_*.c),$(wildcard src/*.c))
OBJ     = $(SRC:src/%.c=$(BUILD)/%.o)
DEP     = $(OBJ:.o=.d)

.PHONY: all test clean asan

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $@

# Sanitizer build (ASan + UBSan) into a separate binary and object dir.
asan:
	$(MAKE) all BUILD=build-asan TARGET=gpx2fit-asan \
	    CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer" \
	    LDFLAGS="-fsanitize=address,undefined"

# Tests: every tests/test_*.c is built against all of src/ except main.c,
# with ASan + UBSan (any report aborts the test), then run.
LIB_SRC   = $(filter-out src/main.c,$(SRC))
TEST_SRC  = $(wildcard tests/test_*.c)
TEST_BIN  = $(TEST_SRC:tests/%.c=build-test/%)
SAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined \
            -fno-omit-frame-pointer

build-test/%: tests/%.c $(LIB_SRC) $(wildcard src/*.h) | build-test
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic $(SAN_FLAGS) -Isrc \
	    -o $@ $< $(LIB_SRC) $(LDLIBS)

build-test:
	mkdir -p $@

test: all $(TEST_BIN)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; ./$$t; done
	@set -e; for s in tests/test_*.sh; do echo "== $$s"; bash $$s ./$(TARGET); done

clean:
	rm -rf $(BUILD) build-asan build-test $(TARGET) $(TARGET)-asan

-include $(DEP)
