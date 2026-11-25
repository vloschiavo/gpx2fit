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

PREFIX ?= /usr/local

.PHONY: all test clean asan test-asan install wasm web-version web-sample

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# symmap.c and the WASM bridge include the generated header. (After the first
# build the -MMD dependency files cover this too; these lines cover the very
# first one.)
$(BUILD)/symmap.o: $(GEN_HDR)
$(BUILD)/wasm_api.o: $(GEN_HDR)

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

# WebAssembly library for the browser bridge. Keep the native CLI entry point
# out of this build; wasm_api.c supplies the exported interface instead.
EMCC       ?= emcc
WASM_DIR   = web/dist
WASM_JS    = $(WASM_DIR)/gpx2fit.js
WASM_WASM  = $(WASM_DIR)/gpx2fit.wasm
WASM_SRC   = $(filter-out src/main.c,$(SRC))
# Expat is not bundled with every Emscripten release. Use its standard setting
# when available and an equivalent pinned local port otherwise.
WASM_EXPAT = $(if $(shell $(EMCC) --show-ports 2>/dev/null | grep -i expat),-sUSE_EXPAT=1,--use-port=tools/expat_port.py)
# ccall copies 'string' arguments to the wasm stack, which overflows on large
# GPX files. malloc/free are exported so the app can pass heap-allocated pointers.
# gpx2fit_get_default_map exposes the embedded default.map to the web UI.
WASM_FLAGS = -O2 -sWASM=1 $(WASM_EXPAT) -sALLOW_MEMORY_GROWTH=1 \
             -sMODULARIZE=1 -sEXPORT_ES6=1 \
             -sEXPORTED_FUNCTIONS='["_malloc","_free","_gpx2fit_convert","_gpx2fit_result_data","_gpx2fit_result_len","_gpx2fit_result_json","_gpx2fit_result_rc","_gpx2fit_get_default_map","_gpx2fit_free"]' \
             -sEXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString","HEAPU8"]'

wasm: $(WASM_SRC) $(wildcard src/*.h) $(GEN_HDR) tools/expat_port.py | $(WASM_DIR)
	$(EMCC) -std=c11 $(WASM_FLAGS) -o $(WASM_JS) $(WASM_SRC)

$(WASM_DIR):
	mkdir -p $@

# The browser sample route is a copy of the reference GPX used to validate the
# CLI. It is checked in so a plain static deployment works; regenerate it after
# editing the reference file. `make web-version` must not depend on this so it
# keeps working on a checkout that has only the web files.
WEB_SAMPLE    = web/Rio_Del_Lago_100mi_2025.gpx
REFERENCE_GPX = reference/Rio_Del_Lago_100mi_2025.gpx

web-sample: $(WEB_SAMPLE)

$(WEB_SAMPLE): $(REFERENCE_GPX)
	cp '$<' '$@'

# Web build stamping (task 15.4). Explicit target: `make`, `make test`,
# `make wasm` and `make clean` never rewrite the tracked web files. Run
# `make web-version` before deploying the bundle (e.g. in the Pages workflow),
# after `make wasm`. The same build id goes into web/version.json, web/sw.js
# (both BUILD_ID and CACHE_NAME) and the meta tag in web/index.html.
# Override WEB_BUILD_ID / WEB_SHA / WEB_BUILD_TIME for reproducible builds.
# Use simple (immediately expanded) variables: $(shell ...) must run exactly
# once, otherwise a build that crosses a second boundary can stamp three
# different ids into the three artifacts. `?=` alone is recursive.
ifeq ($(origin WEB_SHA),undefined)
WEB_SHA := $(shell git rev-parse --short=12 HEAD 2>/dev/null || printf unknown)
endif
ifeq ($(origin WEB_BUILD_TIME),undefined)
WEB_BUILD_TIME := $(shell date -u +%Y%m%dT%H%M%SZ)
endif
ifeq ($(origin WEB_BUILD_ID),undefined)
WEB_BUILD_ID := $(WEB_SHA)-$(WEB_BUILD_TIME)
endif

web-version:
	@case '$(WEB_BUILD_ID)' in \
	  ''|*[!A-Za-z0-9._-]*) echo "web-version: invalid WEB_BUILD_ID '$(WEB_BUILD_ID)'" >&2; exit 1;; \
	esac
	@printf '{\n  "build_id": "%s",\n  "sha": "%s",\n  "built_at": "%s"\n}\n' \
	    '$(WEB_BUILD_ID)' '$(WEB_SHA)' '$(WEB_BUILD_TIME)' > web/version.json.tmp
	@mv web/version.json.tmp web/version.json
	@sed -e "s|^\(const BUILD_ID = \)'[^']*';|\1'$(WEB_BUILD_ID)';|" \
	     -e "s|^\(const CACHE_NAME = '\)gpx2fit-[^']*';|\1gpx2fit-$(WEB_BUILD_ID)';|" \
	     web/sw.js > web/sw.js.tmp
	@mv web/sw.js.tmp web/sw.js
	@sed -e 's|\(<meta id="gpx2fit-build" name="gpx2fit-build" content="\)[^"]*\("\)|\1$(WEB_BUILD_ID)\2|' \
	     web/index.html > web/index.html.tmp
	@mv web/index.html.tmp web/index.html
	@echo "Stamped web build $(WEB_BUILD_ID) (sha $(WEB_SHA), built_at $(WEB_BUILD_TIME))"

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


# Fuzzing the GPX reader (task 10.2). Not part of `make test`.
# `make fuzz` builds tests/fuzz/fuzz_gpx.c with the gcc-only mutation driver
# tests/fuzz/fuzz_driver.c (ASan + UBSan, any report aborts) and runs it for
# FUZZ_SECONDS, seeded from tests/data/*.gpx. A crashing input is saved to
# $(FUZZ_OUT)/crash-<n>.gpx; replay it with ./build-fuzz/fuzz_gpx FILE.
# `make fuzz-libfuzzer` is the same harness under clang's libFuzzer.
FUZZ_SECONDS ?= 300
FUZZ_SEED    ?= $(shell date +%s)
FUZZ_OUT     ?= build-fuzz/out
FUZZ_CC      ?= clang
FUZZ_SRC     = tests/fuzz/fuzz_gpx.c
FUZZ_DEPS    = $(FUZZ_SRC) $(LIB_SRC) $(wildcard src/*.h) $(GEN_HDR)

.PHONY: fuzz fuzz-libfuzzer

build-fuzz:
	mkdir -p $@

build-fuzz/fuzz_gpx: tests/fuzz/fuzz_driver.c $(FUZZ_DEPS) | build-fuzz
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic $(SAN_FLAGS) -Isrc \
	    -o $@ tests/fuzz/fuzz_driver.c $(FUZZ_SRC) $(LIB_SRC) $(LDLIBS)

fuzz: build-fuzz/fuzz_gpx
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 \
	./build-fuzz/fuzz_gpx -t $(FUZZ_SECONDS) -s $(FUZZ_SEED) \
	    -o $(FUZZ_OUT) tests/data

build-fuzz/fuzz_gpx_libfuzzer: $(FUZZ_DEPS) | build-fuzz
	$(FUZZ_CC) -O1 -g -std=c11 -fsanitize=fuzzer,address,undefined \
	    -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Isrc \
	    -o $@ $(FUZZ_SRC) $(LIB_SRC) $(LDLIBS)

fuzz-libfuzzer: build-fuzz/fuzz_gpx_libfuzzer
	mkdir -p build-fuzz/corpus $(FUZZ_OUT)
	cp tests/data/*.gpx build-fuzz/corpus/
	./build-fuzz/fuzz_gpx_libfuzzer -max_total_time=$(FUZZ_SECONDS) \
	    -artifact_prefix=$(FUZZ_OUT)/ build-fuzz/corpus

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -rf $(BUILD) build-asan build-test build-fuzz $(TARGET) $(TARGET)-asan $(GEN_HDR) $(GEN_HDR).tmp
	rm -f $(WASM_JS) $(WASM_WASM)

-include $(DEP)
