CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
LDLIBS  =

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

# Test runner: no tests exist yet; later tasks add tests/ programs here.
test: all
	@echo "No tests yet."

clean:
	rm -rf $(BUILD) build-asan $(TARGET) $(TARGET)-asan

-include $(DEP)
