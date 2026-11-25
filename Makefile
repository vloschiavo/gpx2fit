# Toolchain and output locations. Every value here may be overridden on the
# make command line (or in the environment), so a target-specific build never
# has to reuse the native artifacts:
#   make CC=x86_64-w64-mingw32-gcc TARGET_OS=Windows TARGET_ARCH=x86_64
#   make CPPFLAGS=-I/opt/homebrew/include LDLIBS='-L/opt/homebrew/lib -lexpat -lm'
# Command-line overrides take precedence over the defaults below.
CC       ?= cc
AR       ?= ar
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS  ?=

# GNU make has a built-in LD=ld, which is not a usable C linker: replace only
# that built-in default with the C compiler. An explicit LD=... is respected.
ifeq ($(origin LD),default)
LD := $(CC)
endif

# Libraries are split into replaceable pieces for sysroots / Homebrew / MinGW;
# overriding LDLIBS as a whole still takes complete control.
EXPAT_CFLAGS ?=
EXPAT_LIBS   ?= -lexpat
MATH_LIBS    ?= -lm
LDLIBS       ?= $(EXPAT_LIBS) $(MATH_LIBS)

# HOST_* describes the machine running make, TARGET_* the machine that will run
# the binary. OS is often unset on Unix, so fall back to uname, and normalize
# aliases: naming the native platform explicitly (Linux, x86_64, Windows for
# Windows_NT/MINGW/MSYS/CYGWIN) must not turn a native build into a cross build.
HOST_OS_RAW   ?= $(if $(strip $(OS)),$(OS),$(shell uname -s 2>/dev/null || printf unknown))
HOST_ARCH_RAW ?= $(shell uname -m 2>/dev/null || printf unknown)
TARGET_OS_RAW   ?= $(HOST_OS_RAW)
TARGET_ARCH_RAW ?= $(HOST_ARCH_RAW)

norm_os = $(strip $(if $(filter Linux linux,$(1)),Linux, \
          $(if $(filter Darwin darwin Macintosh%,$(1)),Darwin, \
          $(if $(filter Windows% windows% MINGW% mingw% MSYS% msys% CYGWIN% cygwin%,$(1)),Windows,$(1)))))
norm_arch = $(strip $(if $(filter x86_64 amd64 x64 X86_64 AMD64 X64,$(1)),x86_64, \
            $(if $(filter aarch64 arm64 AARCH64 ARM64,$(1)),aarch64, \
            $(if $(filter i386 i486 i586 i686 x86 I386 I486 I586 I686 X86,$(1)),x86,$(1)))))

HOST_OS     ?= $(call norm_os,$(HOST_OS_RAW))
HOST_ARCH   ?= $(call norm_arch,$(HOST_ARCH_RAW))
TARGET_OS   ?= $(call norm_os,$(TARGET_OS_RAW))
TARGET_ARCH ?= $(call norm_arch,$(TARGET_ARCH_RAW))

# Command-line overrides remain raw, so every decision uses normalized copies.
HOST_OS_N     := $(call norm_os,$(HOST_OS))
HOST_ARCH_N   := $(call norm_arch,$(HOST_ARCH))
TARGET_OS_N   := $(call norm_os,$(TARGET_OS))
TARGET_ARCH_N := $(call norm_arch,$(TARGET_ARCH))

# Windows-family targets (native MSYS2/MinGW or a mingw cross compiler) need
# the .exe suffix. EXE_EXT can always be set explicitly.
ifeq ($(TARGET_OS_N),Windows)
EXE_EXT ?= .exe
else
EXE_EXT ?=
endif

# CROSS=0/1 may be forced on the command line; otherwise it is inferred from
# a normalized OS/architecture difference. Cross builds never build or run
# host-only (sanitizer) binaries.
CROSS ?= 0
ifneq ($(HOST_OS_N),$(TARGET_OS_N))
CROSS := 1
endif
ifneq ($(HOST_ARCH_N),$(TARGET_ARCH_N))
CROSS := 1
endif
CROSS_ACTIVE = $(if $(filter 0,$(strip $(CROSS))),0,1)

# Suffix for artifacts of a non-native target, keeping them out of the way of
# native builds. BUILD/TARGET themselves remain freely overridable.
TARGET_LABEL :=
ifneq ($(HOST_OS_N),$(TARGET_OS_N))
TARGET_LABEL := $(TARGET_OS_N)
endif
ifneq ($(HOST_ARCH_N),$(TARGET_ARCH_N))
TARGET_LABEL := $(TARGET_LABEL)$(if $(TARGET_LABEL),-)$(TARGET_ARCH_N)
endif
# A forced CROSS=1 without a platform change still gets separate artifacts.
ifeq ($(CROSS_ACTIVE)$(strip $(TARGET_LABEL)),1)
TARGET_LABEL := cross
endif

BUILD  ?= build$(if $(TARGET_LABEL),-$(TARGET_LABEL),)
TARGET ?= gpx2fit$(if $(TARGET_LABEL),-$(TARGET_LABEL),)$(EXE_EXT)

CROSS_DESC = $(if $(TARGET_LABEL),$(TARGET_LABEL),$(TARGET_OS_N)/$(TARGET_ARCH_N))

SAN_FLAGS ?= -fsanitize=address,undefined -fno-sanitize-recover=undefined \
             -fno-omit-frame-pointer

# One authoritative version, defined in src/version.h. Read the same string
# here for tooling (release tags, web build metadata) so it cannot drift from
# what `gpx2fit --version` prints. `make version` exposes it to scripts.
VERSION := $(shell sed -n 's/^#define GPX2FIT_VERSION "\([^"]*\)"/\1/p' src/version.h)

# data/default.map is embedded into the binary as a C string (task 5.3).
# Needs xxd (ships with vim). The header is regenerated when the map changes.
GEN_HDR = src/default_map.h

SRC     = $(filter-out $(wildcard src/test_*.c),$(wildcard src/*.c))
OBJ     = $(SRC:src/%.c=$(BUILD)/%.o)
DEP     = $(OBJ:.o=.d)

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin

.PHONY: all lib version test clean asan test-asan install wasm web-version web-sample

all: $(TARGET)

version:
	@if [ -z '$(VERSION)' ]; then \
	    echo "version: cannot read GPX2FIT_VERSION from src/version.h" >&2; \
	    exit 1; \
	fi
	@printf '%s\n' '$(VERSION)'

$(TARGET): $(OBJ)
	$(LD) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Optional static library of the reusable modules (main.c excluded). This is
# where AR is used, so an alternative archiver can be supplied for a target.
LIB     ?= $(BUILD)/libgpx2fit.a
LIB_OBJ  = $(filter-out $(BUILD)/main.o,$(OBJ))

lib: $(LIB)

$(LIB): $(LIB_OBJ) | $(BUILD)
	$(AR) rcs $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(EXPAT_CFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

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

# Sanitizer build (ASan + UBSan) into a separate binary and object dir. Names
# are derived from the current BUILD/TARGET, so an overridden build cannot
# collide with the plain one. Skipped for cross builds: the instrumented binary
# could not be run here.
ASAN_BUILD   ?= $(BUILD)-asan
ASAN_TARGET  ?= $(basename $(TARGET))-asan$(suffix $(TARGET))
ASAN_CFLAGS  ?= -O1 -g -std=c11 -Wall -Wextra -Wpedantic \
                -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_LDFLAGS ?= -fsanitize=address,undefined

asan:
	@if [ '$(CROSS_ACTIVE)' != 0 ]; then \
	    echo "asan: skipping (cross build for $(CROSS_DESC))"; \
	else \
	    $(MAKE) all BUILD='$(ASAN_BUILD)' TARGET='$(ASAN_TARGET)' \
	        CFLAGS='$(ASAN_CFLAGS)' LDFLAGS='$(LDFLAGS) $(ASAN_LDFLAGS)'; \
	fi

# Tests: every tests/test_*.c is built against all of src/ except main.c,
# with ASan + UBSan (any report aborts the test), then run.
# Then every tests/test_*.sh and the end-to-end tests/run.sh run against the
# built binary.
LIB_SRC    = $(filter-out src/main.c,$(SRC))
TEST_SRC   = $(wildcard tests/test_*.c)
TEST_BUILD ?= build-test
TEST_BIN   = $(TEST_SRC:tests/%.c=$(TEST_BUILD)/%)
TEST_SH    = $(wildcard tests/test_*.sh) tests/run.sh

$(TEST_BUILD)/%: tests/%.c $(LIB_SRC) $(wildcard src/*.h) $(GEN_HDR) | $(TEST_BUILD)
	$(CC) $(CPPFLAGS) $(EXPAT_CFLAGS) -O1 -g -std=c11 -Wall -Wextra -Wpedantic $(SAN_FLAGS) -Isrc \
	    $(LDFLAGS) -o $@ $< $(LIB_SRC) $(LDLIBS)

$(TEST_BUILD):
	mkdir -p $@

# WebAssembly library for the browser bridge. Keep the native CLI entry point
# out of this build; wasm_api.c supplies the exported interface instead.
EMCC       ?= emcc
WASM_DIR   = web/dist
WASM_JS    = $(WASM_DIR)/gpx2fit.js
WASM_WASM  = $(WASM_DIR)/gpx2fit.wasm
WASM_SRC   = $(filter-out src/main.c,$(SRC))
# Expat is not bundled with every Emscripten release. Use its standard setting
# when available and an equivalent pinned local port otherwise. Override
# WASM_EXPAT to skip the emcc probe entirely (e.g. with a cross toolchain).
WASM_EXPAT ?= $(if $(shell $(EMCC) --show-ports 2>/dev/null | grep -i expat),-sUSE_EXPAT=1,--use-port=tools/expat_port.py)
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

# Web build stamping (task 15.4). VERSION above comes from src/version.h:
# release tags and web metadata that names a release should use it instead of
# a second copy of the version string. The build id below is per-build.
# Explicit target: `make`, `make test`,
# `make wasm` and `make clean` never rewrite the tracked web files. Run
# `make web-version` before deploying the bundle (e.g. in the Pages workflow),
# after `make wasm`. The same build id goes into web/version.json, web/sw.js
# (both BUILD_ID and CACHE_NAME) and the meta tag in web/index.html. That step
# also stamps the footer version label from VERSION (src/version.h), so the web
# UI always shows the same release as `gpx2fit --version`.
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
	@test -n '$(VERSION)' || { echo "web-version: cannot read GPX2FIT_VERSION from src/version.h" >&2; exit 1; }
	@printf '{\n  "build_id": "%s",\n  "sha": "%s",\n  "built_at": "%s"\n}\n' \
	    '$(WEB_BUILD_ID)' '$(WEB_SHA)' '$(WEB_BUILD_TIME)' > web/version.json.tmp
	@mv web/version.json.tmp web/version.json
	@sed -e "s|^\(const BUILD_ID = \)'[^']*';|\1'$(WEB_BUILD_ID)';|" \
	     -e "s|^\(const CACHE_NAME = '\)gpx2fit-[^']*';|\1gpx2fit-$(WEB_BUILD_ID)';|" \
	     web/sw.js > web/sw.js.tmp
	@mv web/sw.js.tmp web/sw.js
	@sed -e 's|\(<meta id="gpx2fit-build" name="gpx2fit-build" content="\)[^"]*\("\)|\1$(WEB_BUILD_ID)\2|' \
	     -e 's|\(<span id="gpx2fit-version" class="version-label">\)[^<]*\(</span>\)|\1$(VERSION)\2|' \
	     web/index.html > web/index.html.tmp
	@mv web/index.html.tmp web/index.html
	@echo "Stamped web build $(WEB_BUILD_ID) (sha $(WEB_SHA), built_at $(WEB_BUILD_TIME))"

# The unit tests are ASan/UBSan binaries and the shell tests execute the built
# CLI: both are host-only. A cross build skips them rather than trying to run
# binaries for another architecture.
TEST_DEPS = $(if $(filter 0,$(CROSS_ACTIVE)),$(TEST_BIN))
# An absolute TARGET must not be prefixed with ./ for the shell tests.
TEST_TARGET      = $(if $(filter /%,$(TARGET)),$(TARGET),./$(TARGET))
ASAN_TEST_TARGET = $(if $(filter /%,$(ASAN_TARGET)),$(ASAN_TARGET),./$(ASAN_TARGET))

test: all $(TEST_DEPS)
	@if [ '$(CROSS_ACTIVE)' != 0 ]; then \
	    echo "== cross build ($(CROSS_DESC)): skipping host-only tests"; \
	else \
	    set -e; \
	    for t in $(TEST_BIN); do echo "== $$t"; ./$$t; done; \
	    for s in $(TEST_SH); do echo "== $$s"; bash $$s $(TEST_TARGET); done; \
	fi

# Shell / end-to-end tests against the ASan+UBSan binary (task 10.1).
# Any sanitizer report aborts the binary, which the tests see as a failure.
# Not part of `make test` (the unit tests there already use sanitizers).
test-asan: asan
	@if [ '$(CROSS_ACTIVE)' != 0 ]; then \
	    echo "test-asan: skipping (cross build for $(CROSS_DESC))"; \
	else \
	    set -e; \
	    for s in $(TEST_SH); do echo "== $$s (asan)"; \
	        ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	        UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	        bash $$s $(ASAN_TEST_TARGET); done; \
	fi


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
# Fuzzers run instrumented host binaries; no point building them when cross.
FUZZ_BIN     = $(if $(filter 0,$(CROSS_ACTIVE)),build-fuzz/fuzz_gpx)
FUZZ_LF_BIN  = $(if $(filter 0,$(CROSS_ACTIVE)),build-fuzz/fuzz_gpx_libfuzzer)

.PHONY: fuzz fuzz-libfuzzer

build-fuzz:
	mkdir -p $@

build-fuzz/fuzz_gpx: tests/fuzz/fuzz_driver.c $(FUZZ_DEPS) | build-fuzz
	$(CC) $(CPPFLAGS) $(EXPAT_CFLAGS) -O1 -g -std=c11 -Wall -Wextra -Wpedantic $(SAN_FLAGS) -Isrc \
	    $(LDFLAGS) -o $@ tests/fuzz/fuzz_driver.c $(FUZZ_SRC) $(LIB_SRC) $(LDLIBS)

fuzz: $(FUZZ_BIN)
	@if [ '$(CROSS_ACTIVE)' != 0 ]; then \
	    echo "fuzz: skipping (cross build for $(CROSS_DESC))"; \
	else \
	    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:abort_on_error=1 \
	    ./build-fuzz/fuzz_gpx -t $(FUZZ_SECONDS) -s $(FUZZ_SEED) \
	        -o $(FUZZ_OUT) tests/data; \
	fi

build-fuzz/fuzz_gpx_libfuzzer: $(FUZZ_DEPS) | build-fuzz
	$(FUZZ_CC) $(CPPFLAGS) $(EXPAT_CFLAGS) -O1 -g -std=c11 \
	    -fsanitize=fuzzer,address,undefined \
	    -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Isrc \
	    $(LDFLAGS) -o $@ $(FUZZ_SRC) $(LIB_SRC) $(LDLIBS)

fuzz-libfuzzer: $(FUZZ_LF_BIN)
	@if [ '$(CROSS_ACTIVE)' != 0 ]; then \
	    echo "fuzz-libfuzzer: skipping (cross build for $(CROSS_DESC))"; \
	else \
	    mkdir -p build-fuzz/corpus $(FUZZ_OUT); \
	    cp tests/data/*.gpx build-fuzz/corpus/; \
	    ./build-fuzz/fuzz_gpx_libfuzzer -max_total_time=$(FUZZ_SECONDS) \
	        -artifact_prefix=$(FUZZ_OUT)/ build-fuzz/corpus; \
	fi

# Install under $(DESTDIR)$(BINDIR) (BINDIR defaults to $(PREFIX)/bin). The
# installed name is the basename of TARGET, so a target built in a subdirectory
# still installs as a single file.
install: $(TARGET)
	install -d '$(DESTDIR)$(BINDIR)'
	install -m 0755 '$(TARGET)' '$(DESTDIR)$(BINDIR)/$(notdir $(TARGET))'

clean:
	rm -rf '$(BUILD)' '$(ASAN_BUILD)' '$(TEST_BUILD)' build-fuzz
	rm -f '$(TARGET)' '$(ASAN_TARGET)' '$(GEN_HDR)' '$(GEN_HDR).tmp'
	rm -f $(WASM_JS) $(WASM_WASM)

-include $(DEP)
