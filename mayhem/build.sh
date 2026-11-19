#!/usr/bin/env bash
#
# mayhem/build.sh — build nccl's fuzz harnesses and the tuner-plugin unit tests.
#
# 1) libnccl_static.a with upstream's own Makefile (`make src.staticlib`), host code compiled with
#    $SANITIZER_FLAGS + SanCov + $DEBUG_FLAGS (passed through CXXFLAGS, which common.mk appends
#    to its own). Device code is compiled by nvcc for a single arch (sm_80) and only the SendRecv
#    collective kernels (ONLY_FUNCS): no GPU is ever present, so kernels are linked, never run.
#    CUDA comes from the in-image redist
#    components under $CUDA_HOME (mayhem/Dockerfile); cudart is linked statically.
# 2) Each harness in mayhem/harnesses/ twice: libFuzzer binary + -standalone reproducer.
# 3) The tests test.sh runs, with normal flags: the example tuner plugin's upstream unit tests
#    (plugins/tuner/example/test), and mayhem/tests/known_answer.cpp against a second
#    libnccl_static.a built by the same upstream Makefile without sanitizers (build/test-lib).
#
# Build trees live under build/ (gitignored by upstream's `/build*`), so `git clean -ffdX` resets
# them and a re-run on a built tree is incremental.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${CUDA_HOME:=/opt/toolchains/deps/cuda-13.2}"
: "${NVCC_GENCODE:=-gencode=arch=compute_80,code=sm_80}"
# Upstream's knob for building a subset of the collective kernels (src/device/generate.py). No
# kernel ever runs here, so only SendRecv is generated; this halves the build.
: "${ONLY_FUNCS:=SendRecv}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS CUDA_HOME

cd "$SRC"

# SanCov on the library itself, so the fuzzer sees coverage of the code it is fuzzing.
COV_FLAGS=""
case "$LIB_FUZZING_ENGINE" in *-fsanitize=fuzzer*) COV_FLAGS="-fsanitize=fuzzer-no-link" ;; esac

FUZZ_BUILD="$SRC/build/fuzz"
OUT="$SRC/build/mayhem"
mkdir -p "$OUT"

# 1) the library (upstream Makefile; CXXFLAGS from the environment is appended by common.mk)
CXXFLAGS="$SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS" \
  make -j"$MAYHEM_JOBS" src.staticlib \
    BUILDDIR="$FUZZ_BUILD" CXX="$CXX" CUDA_HOME="$CUDA_HOME" CUDA_LIB="$CUDA_HOME/lib" \
    NVCC_GENCODE="$NVCC_GENCODE" ONLY_FUNCS="$ONLY_FUNCS"

CUDA_MAJOR=$("$CUDA_HOME/bin/nvcc" --version | sed -n 's/.*release \([0-9]*\)\.\([0-9]*\).*/\1/p')
CUDA_MINOR=$("$CUDA_HOME/bin/nvcc" --version | sed -n 's/.*release \([0-9]*\)\.\([0-9]*\).*/\2/p')
# The same defines and include path the library's own objects were compiled with
# (makefiles/common.mk, src/Makefile), so the harnesses see identical struct layouts.
NCCL_DEFS="-DCUDA_MAJOR=$CUDA_MAJOR -DCUDA_MINOR=$CUDA_MINOR -DNCCL_OS_LINUX -DNCCL_GIN_PROXY_ENABLE=1 \
  -DDOCA_GPUNETIO_USE_CUDA_WRAPPER -DDOCA_VERBS_USE_NET_WRAPPER -DPROFAPI"
NCCL_INCS="-I$SRC/src -I$FUZZ_BUILD/include -I$FUZZ_BUILD/obj/include -I$SRC/src/include \
  -I$SRC/src/include/plugin -I$SRC/src/graph -I$SRC/src/transport/net_ib/gdaki/doca-gpunetio/include \
  -I$CUDA_HOME/include -I$CUDA_HOME/include/cccl -I$SRC/mayhem/harnesses"
NCCL_LIBS="$FUZZ_BUILD/lib/libnccl_static.a -L$CUDA_HOME/lib -lcudart_static -lpthread -ldl -lrt"

# 2) harnesses
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

for h in fuzz_xml fuzz_topo_system fuzz_graph_xml fuzz_proxy_service fuzz_env_selectors fuzz_env_file; do
  # shellcheck disable=SC2086
  $CXX $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS -std=c++17 $NCCL_DEFS $NCCL_INCS \
    -c "$SRC/mayhem/harnesses/$h.cpp" -o "$OUT/$h.o"
  # shellcheck disable=SC2086
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/$h.o" "$OUT/lsan_off.o" $NCCL_LIBS -o "/mayhem/$h"
  # shellcheck disable=SC2086
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/$h.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" $NCCL_LIBS \
    -o "/mayhem/$h-standalone"
done

# The example tuner plugin is plain C with its own headers (plugins/tuner/example/nccl).
TUNER="$SRC/plugins/tuner/example"
$CC $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS -fPIC -I"$TUNER/nccl" -c "$TUNER/plugin.c" -o "$OUT/tuner_plugin.o"
$CC $SANITIZER_FLAGS $COV_FLAGS $DEBUG_FLAGS -I"$TUNER/nccl" -I"$SRC/mayhem/harnesses" \
  -c "$SRC/mayhem/harnesses/fuzz_tuner_config.c" -o "$OUT/fuzz_tuner_config.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz_tuner_config.o" "$OUT/tuner_plugin.o" \
  "$OUT/lsan_off.o" -o /mayhem/fuzz_tuner_config
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz_tuner_config.o" "$OUT/tuner_plugin.o" "$OUT/standalone_main.o" \
  "$OUT/lsan_off.o" -o /mayhem/fuzz_tuner_config-standalone

# 3) upstream's tuner unit tests (plugins/tuner/example/test/Makefile recipe, normal flags; the
#    test includes ../plugin.c itself), built out of tree so test.sh only runs it.
TEST_OUT="$SRC/build/test"
mkdir -p "$TEST_OUT"
# shellcheck disable=SC2086
$CC -Wall -Wextra -g -std=c99 -fPIC $COVERAGE_FLAGS -I"$TUNER/test" -I"$TUNER/nccl" \
  -o "$TEST_OUT/test_plugin" "$TUNER/test/test_plugin.c"

# shellcheck disable=SC2086
env -u CXXFLAGS CXXFLAGS="$COVERAGE_FLAGS" \
  make -j"$MAYHEM_JOBS" src.staticlib \
    BUILDDIR="$SRC/build/test-lib" CXX="$CXX" CUDA_HOME="$CUDA_HOME" CUDA_LIB="$CUDA_HOME/lib" \
    NVCC_GENCODE="$NVCC_GENCODE" ONLY_FUNCS="$ONLY_FUNCS"
TEST_INCS="${NCCL_INCS//$FUZZ_BUILD/$SRC/build/test-lib}"
# shellcheck disable=SC2086
$CXX -O1 -g -std=c++17 $COVERAGE_FLAGS $NCCL_DEFS $TEST_INCS "$SRC/mayhem/tests/known_answer.cpp" \
  "$SRC/build/test-lib/lib/libnccl_static.a" -L"$CUDA_HOME/lib" -lcudart_static -lpthread -ldl -lrt \
  -o "$TEST_OUT/known_answer"

echo "build.sh: done"
