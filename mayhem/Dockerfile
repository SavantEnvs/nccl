# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# CUDA for the upstream build (nvcc for the device objects, cuda_runtime.h/cuda.h/CCCL headers,
# static cudart). NVIDIA's redistributable component archives, pinned by version + sha256 (from
# https://developer.download.nvidia.com/compute/cuda/redist/redistrib_13.2.0.json), merged into one
# CUDA_HOME-style prefix under /opt/toolchains/deps (SPEC §6.2 item 19). They ship binaries only, so
# there is no src/. CUDA 13 rather than 12.x because 12.x's crt/math_functions.h conflicts with
# trixie's glibc 2.41 (cospi/sinpi exception specifications). The shared libcudart is removed so
# everything links the static one, as nccl does by default (CUDARTLIB=cudart_static).
USER root
ARG CUDA_REDIST=https://developer.download.nvidia.com/compute/cuda/redist
RUN set -eux; d=/opt/toolchains/deps/cuda-13.2; mkdir -p "$d" /tmp/cuda; cd /tmp/cuda; \
    for spec in \
      "cuda_cudart/linux-x86_64/cuda_cudart-linux-x86_64-13.2.51-archive.tar.xz 539edc1056e44d319f2112e9971c6415d78d4dde04b3f6ffbd20ec808e718526" \
      "cuda_nvcc/linux-x86_64/cuda_nvcc-linux-x86_64-13.2.51-archive.tar.xz 706b996fefc59dc8d64d317fdf48d0aa84c4ae004eff43009dd918f40c5cc66a" \
      "cuda_crt/linux-x86_64/cuda_crt-linux-x86_64-13.2.51-archive.tar.xz fbc31fed55b7255591f3a19f575ca078827f5e6757d317d009f7ec1e69fcde4b" \
      "cuda_culibos/linux-x86_64/cuda_culibos-linux-x86_64-13.2.51-archive.tar.xz dece3cdb7954d276e07eb23bee882a24627f431654cdeeef2bcee2d22669d93c" \
      "libnvvm/linux-x86_64/libnvvm-linux-x86_64-13.2.51-archive.tar.xz e013fce38130d2337ea695aadc5ddd5dcfb78f9107903d72492b9819539749bb" \
      "cuda_cccl/linux-x86_64/cuda_cccl-linux-x86_64-13.2.27-archive.tar.xz 56e1bafb29faa87375b0484814870046530b88c0a421909096892f027ec1927b" ; do \
      set -- $spec; curl -fsSL -o a.tar.xz "$CUDA_REDIST/$1"; echo "$2  a.tar.xz" | sha256sum -c -; \
      tar -xJf a.tar.xz --strip-components=1 --no-same-owner -C "$d"; rm a.tar.xz; done; \
    rm -f "$d"/lib/libcudart.so*; cd /; rm -rf /tmp/cuda; chmod -R a+rX,go-w "$d"
ENV CUDA_HOME=/opt/toolchains/deps/cuda-13.2
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
