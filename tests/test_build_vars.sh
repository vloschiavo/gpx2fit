#!/usr/bin/env bash
# Build-variable tests (T3): isolated BUILD/TARGET outputs, toolchain and flag
# overrides, Windows .exe suffix, and cross-build safety.
# Usage: tests/test_build_vars.sh [path/to/gpx2fit]
BIN=${1:-./gpx2fit}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
fail=0
TMP=$(mktemp -d) || exit 1
trap 'rm -rf "$TMP"' EXIT

ok()  { echo "ok   - $1"; }
bad() { echo "FAIL - $1"; fail=1; }
has() { printf '%s\n' "$1" | grep -qxF -- "$2"; }

# Recursive make without the outer test run's jobserver flags.
run_make() {
    MAKEFLAGS= make -C "$ROOT" --no-print-directory "$@" >"$TMP/make.log" 2>&1
}

# Command-line variables are exported to recipes, so a `make test BUILD=...`
# run would otherwise leak into these checks. Clear them, then ask make for the
# derived variables instead of inspecting recipe text from a dry run.
clean_make() {
    env -u MAKEFLAGS -u MFLAGS -u OS \
        -u BUILD -u TARGET -u CROSS \
        -u HOST_OS -u HOST_ARCH -u HOST_OS_RAW -u HOST_ARCH_RAW \
        -u TARGET_OS -u TARGET_ARCH -u TARGET_OS_RAW -u TARGET_ARCH_RAW \
        -u EXE_EXT \
        make -C "$ROOT" --no-print-directory "$@"
}

cat >"$TMP/print.mk" <<'EOF'
print-build-vars:
	@printf 'TARGET=[%s]\nBUILD=[%s]\nCROSS_ACTIVE=[%s]\nEXE_EXT=[%s]\nTEST_DEPS_COUNT=[%s]\n' '$(TARGET)' '$(BUILD)' '$(CROSS_ACTIVE)' '$(EXE_EXT)' '$(words $(TEST_DEPS))'
EOF
print_vars() {
    clean_make -s -f Makefile -f "$TMP/print.mk" "$@" print-build-vars
}
test_count() {
    printf '%s\n' "$1" | sed -n 's/^TEST_DEPS_COUNT=\[\([0-9]*\)\]$/\1/p'
}

# 1. Custom BUILD and TARGET keep objects and the binary out of the native tree.
B1=$TMP/build-custom
T1=$TMP/gpx2fit-custom
if run_make BUILD="$B1" TARGET="$T1" all && [ -f "$B1/main.o" ] && [ -x "$T1" ]; then
    ok "custom BUILD/TARGET produce isolated objects and binary"
else
    bad "custom BUILD/TARGET build"; sed -n '1,20p' "$TMP/make.log"
fi
if "$T1" --version >/dev/null 2>&1; then
    ok "custom TARGET binary is runnable"
else
    bad "custom TARGET binary is runnable"
fi

# 2. Toolchain/flag overrides are honoured and still link a working binary.
B2=$TMP/build-override
T2=$TMP/gpx2fit-override
if run_make BUILD="$B2" TARGET="$T2" CC="${CC:-cc}" LD="${CC:-cc}" \
        CFLAGS='-O0 -g -std=c11 -Wall -Wextra' CPPFLAGS='-Isrc' \
        LDFLAGS='' LDLIBS='-lexpat -lm' all \
    && [ -x "$T2" ] && "$T2" --version >/dev/null 2>&1; then
    ok "CC/LD/CFLAGS/CPPFLAGS/LDFLAGS/LDLIBS overrides work"
else
    bad "toolchain/flag overrides"; sed -n '1,20p' "$TMP/make.log"
fi

# 3. AR is overridable via the optional static-library target.
ARLOG=$TMP/ar.log
cat >"$TMP/ar-wrap" <<EOF
#!/bin/sh
echo "AR called: \$*" >>"$ARLOG"
exec ar "\$@"
EOF
chmod +x "$TMP/ar-wrap"
if run_make BUILD="$TMP/build-lib" AR="$TMP/ar-wrap" lib \
    && grep -q 'AR called: rcs' "$ARLOG"; then
    ok "AR override is used for the static library"
else
    bad "AR override is used for the static library"; sed -n '1,20p' "$TMP/make.log"
fi

# 4. A Windows target gets .exe and its own BUILD dir without needing a cross
#    toolchain. Assert on the derived variables, not on compiler output.
v=$(print_vars TARGET_OS=Windows)
if has "$v" 'CROSS_ACTIVE=[1]' && has "$v" 'EXE_EXT=[.exe]' \
    && has "$v" 'BUILD=[build-Windows]' && has "$v" 'TARGET=[gpx2fit-Windows.exe]'; then
    ok "Windows target uses .exe and an isolated BUILD directory"
else
    bad "Windows target naming"; printf '%s\n' "$v"
fi

# 5. Naming the native platform explicitly must not be a cross build.
v=$(print_vars TARGET_OS=Linux)
if has "$v" 'CROSS_ACTIVE=[0]' && has "$v" 'BUILD=[build]' \
    && has "$v" 'TARGET=[gpx2fit]' && [ "$(test_count "$v")" -gt 0 ]; then
    ok "native TARGET_OS=Linux keeps running tests"
else
    bad "native TARGET_OS=Linux keeps running tests"; printf '%s\n' "$v"
fi
v=$(print_vars TARGET_ARCH=x86_64)
if has "$v" 'CROSS_ACTIVE=[0]' && has "$v" 'BUILD=[build]' \
    && has "$v" 'TARGET=[gpx2fit]' && [ "$(test_count "$v")" -gt 0 ]; then
    ok "native TARGET_ARCH=x86_64 keeps running tests"
else
    bad "native TARGET_ARCH=x86_64 keeps running tests"; printf '%s\n' "$v"
fi
v=$(print_vars OS=Windows_NT TARGET_OS=Windows)
if has "$v" 'CROSS_ACTIVE=[0]' && has "$v" 'EXE_EXT=[.exe]' \
    && has "$v" 'BUILD=[build]' && has "$v" 'TARGET=[gpx2fit.exe]'; then
    ok "MSYS2-style OS=Windows_NT with TARGET_OS=Windows is native"
else
    bad "MSYS2-style OS=Windows_NT with TARGET_OS=Windows is native"; printf '%s\n' "$v"
fi

# 6. A real cross target is detected, labeled, and skips host-only binaries.
v=$(print_vars TARGET_ARCH=aarch64)
if has "$v" 'CROSS_ACTIVE=[1]' && has "$v" 'BUILD=[build-aarch64]' \
    && has "$v" 'TARGET=[gpx2fit-aarch64]' && [ "$(test_count "$v")" -eq 0 ]; then
    ok "cross TARGET_ARCH=aarch64 is isolated and skips tests"
else
    bad "cross TARGET_ARCH=aarch64 is isolated and skips tests"; printf '%s\n' "$v"
fi
cross=$(clean_make -n test TARGET_ARCH=aarch64 2>/dev/null)
if printf '%s\n' "$cross" | grep -q 'skipping host-only tests'; then
    ok "cross build skips host-only tests"
else
    bad "cross build skips host-only tests"; printf '%s\n' "$cross" | tail -20
fi
asan=$(clean_make -n asan TARGET_ARCH=aarch64 2>/dev/null)
if printf '%s\n' "$asan" | grep -q 'asan: skipping'; then
    ok "cross build skips ASan build"
else
    bad "cross build skips ASan build"; printf '%s\n' "$asan" | tail -20
fi

# 7. An explicit CROSS=1 works even without a platform change.
v=$(print_vars CROSS=1)
if has "$v" 'CROSS_ACTIVE=[1]' && has "$v" 'BUILD=[build-cross]' \
    && has "$v" 'TARGET=[gpx2fit-cross]' && [ "$(test_count "$v")" -eq 0 ]; then
    ok "explicit CROSS=1 uses labeled artifacts and skips tests"
else
    bad "explicit CROSS=1 uses labeled artifacts and skips tests"; printf '%s\n' "$v"
fi
asan=$(clean_make -n asan CROSS=1 2>/dev/null)
if printf '%s\n' "$asan" | grep -q 'asan: skipping'; then
    ok "explicit CROSS=1 skips ASan build"
else
    bad "explicit CROSS=1 skips ASan build"; printf '%s\n' "$asan" | tail -20
fi

# 8. Install layout stays predictable with DESTDIR/PREFIX and a custom target.
D=$TMP/dest
if run_make BUILD="$B1" TARGET="$T1" DESTDIR="$D" PREFIX=/opt/gpx2fit install \
    && [ -x "$D/opt/gpx2fit/bin/gpx2fit-custom" ]; then
    ok "DESTDIR/PREFIX install uses the target basename"
else
    bad "DESTDIR/PREFIX install"; sed -n '1,20p' "$TMP/make.log"
fi

if [ "$fail" -eq 0 ]; then
    echo "PASS - tests/test_build_vars.sh"
else
    echo "FAIL - tests/test_build_vars.sh"
fi
exit "$fail"
