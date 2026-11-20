#!/usr/bin/env bash
#
# mayhem/build.sh — build NIXL (meson) twice and the fuzz harnesses against it.
#   mayhem/build-fuzz : the core library + abseil, instrumented with $SANITIZER_FLAGS $DEBUG_FLAGS
#   mayhem/build-test : the same with the project's normal flags, for the self-test oracle
# Both build dirs are gitignored (mayhem/.gitignore). Air-gapped: the meson wraps the build
# needs are staged from /opt/toolchains/deps (see mayhem/Dockerfile) and downloads are off.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
DEPS=/opt/toolchains/deps

# 0) Stage the meson subprojects from the in-image cache (subprojects/* is gitignored upstream,
#    so this is redone after every `git clean -ffdX`).
mkdir -p subprojects/packagecache
cp -n "$DEPS"/meson-packagecache/* subprojects/packagecache/
if [ ! -f subprojects/taskflow/meson.build ]; then
  rm -rf subprojects/taskflow
  cp -r "$DEPS"/taskflow-3.10.0/src subprojects/taskflow
  chmod -R u+w subprojects/taskflow
  cp subprojects/packagefiles/taskflow/meson.build subprojects/taskflow/meson.build
fi

# Common meson options: core + POSIX plugin only (the other plugins need vendor SDKs/hardware),
# no upstream tests/examples, no downloads, -Werror off (clang-19 warns on third-party code).
MESON_OPTS=(--wrap-mode=nodownload -Dwerror=false -Denable_plugins=POSIX
            -Dbuild_tests=false -Dbuild_examples=false)

meson_build() {  # <dir> <cxxflags> <ldflags> <buildtype>
  local dir="$1" cxxflags="$2" ldflags="$3" bt="$4"
  if [ ! -f "$dir/build.ninja" ]; then
    CXXFLAGS="$cxxflags" CFLAGS="$cxxflags" LDFLAGS="$ldflags" \
      meson setup "$dir" . --buildtype="$bt" -Db_lundef=false "${MESON_OPTS[@]}"
  fi
  ninja -C "$dir" -j"$MAYHEM_JOBS"
}

# 1) Sanitized library build (project + abseil share the flags: absl's container layout depends
#    on whether ASan is on, so mixing an uninstrumented absl with instrumented nixl breaks the ABI).
#    Upstream links abseil statically into several of its shared libraries (libserdes.so,
#    libnixl_common.so, ...), so absl's globals exist once per library; ASan's ODR check aborts
#    every process on that at load time. Global-variable instrumentation is therefore off for the
#    library build (-asan-globals=0); heap/stack/use-after-free checks and UBSan stay on.
#    The library also gets libFuzzer's coverage instrumentation (-fsanitize=fuzzer-no-link) so the
#    fuzzers see its edges, not just the harness's. Its trace callbacks are resolved by the sanitizer
#    runtime in the -standalone reproducers, so it is only added when a sanitizer is on.
ASAN_GLOBALS_OFF=""
case "$SANITIZER_FLAGS" in *address*) ASAN_GLOBALS_OFF="-mllvm -asan-globals=0" ;; esac
SANCOV=""
[ -z "$SANITIZER_FLAGS" ] || SANCOV="-fsanitize=fuzzer-no-link"
FUZZ_FLAGS="$SANITIZER_FLAGS $SANCOV $ASAN_GLOBALS_OFF $DEBUG_FLAGS"
meson_build mayhem/build-fuzz "$FUZZ_FLAGS -O1" "$SANITIZER_FLAGS $SANCOV" plain

# 2) Normal-flags build for the self-test oracle.
meson_build mayhem/build-test "-O2 $COVERAGE_FLAGS" "$COVERAGE_FLAGS" plain

INC=(-Isrc/api/cpp -Isrc/api/cpp/backend -Isrc/infra -Isrc/core -Isrc/core/telemetry -Isrc/utils
     -Isubprojects/abseil-cpp-20250814.1 -Imayhem/harnesses)
link_args() {  # <builddir>: link the shared nixl libs with an rpath to every built .so dir
  local b="$1" d args=()
  for d in $(find "$b" -name '*.so*' -printf '%h\n' | sort -u); do
    args+=(-L"$SRC/$d" -Wl,-rpath,"$SRC/$d")
  done
  # abseil (subproject) libraries: nixl's logging headers use absl::log inline.
  args+=($(find "$SRC/$b/subprojects" -name 'libabsl*.so' | sort))
  echo "${args[@]} -lnixl -lnixl_build -lserdes -lnixl_common -lpthread -ldl"
}
FUZZ_LINK=$(link_args mayhem/build-fuzz)
TEST_LINK=$(link_args mayhem/build-test)

# The FUZZ backend plugin (mayhem/harnesses/fuzz_backend.h): a remote-capable backend so
# loadRemoteMD stores and checks peer memory sections. It goes in "plugins/" next to libnixl.so,
# the directory the plugin manager searches by default.
build_plugin() {  # <builddir> <flags...>
  local b="$1"; shift
  mkdir -p "$b/src/core/plugins"
  # shellcheck disable=SC2046
  $CXX -std=c++20 -shared -fPIC "$@" "${INC[@]}" mayhem/harnesses/fuzz_backend_plugin.cpp \
    $(link_args "$b") -o "$b/src/core/plugins/libplugin_FUZZ.so"
}
build_plugin mayhem/build-fuzz $FUZZ_FLAGS
build_plugin mayhem/build-test -O1 $COVERAGE_FLAGS

# 3) Harnesses: libFuzzer binary + run-once standalone reproducer each; LSan off (SPEC §6.2 15).
mkdir -p mayhem/out
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o mayhem/out/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o mayhem/out/standalone_main.o
for h in fuzz_load_remote_md fuzz_dlist; do
  # shellcheck disable=SC2086
  $CXX -std=c++20 $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "${INC[@]}" \
    mayhem/harnesses/$h.cpp mayhem/out/lsan_off.o $FUZZ_LINK -o "$SRC/$h"
  # shellcheck disable=SC2086
  $CXX -std=c++20 $SANITIZER_FLAGS $DEBUG_FLAGS "${INC[@]}" \
    mayhem/harnesses/$h.cpp mayhem/out/standalone_main.o mayhem/out/lsan_off.o $FUZZ_LINK \
    -o "$SRC/$h-standalone"
done

# 4) Self-test oracle (normal flags), run by mayhem/test.sh.
# shellcheck disable=SC2086
$CXX -std=c++20 -O1 $COVERAGE_FLAGS "${INC[@]}" mayhem/harnesses/nixl_selftest.cpp $TEST_LINK \
  -o "$SRC/mayhem/out/nixl_selftest"
