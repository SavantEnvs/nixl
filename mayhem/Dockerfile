# syntax=docker/dockerfile:1.7
#
# Commit image for NIXL (ai-dynamo/nixl): meson/ninja C++20 build of the core library,
# fuzzed through its peer-metadata and descriptor-list deserializers.
#
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile --build-arg COMMIT=$(git rev-parse HEAD) -t nixl .
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Build tools + the dependencies apt ships at a version upstream accepts (header-only asio and
# toml++, liburing, pybind11 for the Python bindings meson always builds).
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
        meson pkg-config python3-dev pybind11-dev libasio-dev libtomlplusplus-dev liburing-dev unzip \
 && rm -rf /var/lib/apt/lists/*

# Meson wraps the build would download (SPEC §6.2 item 19), fetched once from pinned sources:
#  - abseil-cpp 20250814.1 + its wrapdb patch: kept as meson package-cache files, which build.sh
#    copies into subprojects/packagecache so meson extracts them without the network. Abseil is
#    built by the project build itself (not uninstrumented from apt): under ASan its
#    flat_hash_map layout changes (ABSL_SWISSTABLE_ENABLE_GENERATIONS), so it must share the
#    sanitizer flags with libnixl or the two disagree on the container ABI.
#  - taskflow v3.10.0 (header-only, a wrap-git upstream): source tree; build.sh stages it as
#    subprojects/taskflow with upstream's packagefiles overlay.
ARG ABSL_VER=20250814.1
ARG ABSL_SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
ARG ABSL_PATCH_SHA256=f44ad4d72ff2919a6a48cf0887f69aef34c338bfdd8931153596e3766d75f654
ARG TASKFLOW_VER=3.10.0
ARG TASKFLOW_SHA256=fe86765da417f6ceaa2d232ffac70c9afaeb3dc0816337d39a7c93e39c2dee0b
RUN d=/opt/toolchains/deps/meson-packagecache && mkdir -p "$d" \
 && curl -fsSL -o "$d/abseil-cpp-${ABSL_VER}.tar.gz" \
      https://github.com/abseil/abseil-cpp/releases/download/${ABSL_VER}/abseil-cpp-${ABSL_VER}.tar.gz \
 && echo "${ABSL_SHA256}  $d/abseil-cpp-${ABSL_VER}.tar.gz" | sha256sum -c - \
 && curl -fsSL -o "$d/abseil-cpp_${ABSL_VER}-1_patch.zip" \
      https://wrapdb.mesonbuild.com/v2/abseil-cpp_${ABSL_VER}-1/get_patch \
 && echo "${ABSL_PATCH_SHA256}  $d/abseil-cpp_${ABSL_VER}-1_patch.zip" | sha256sum -c - \
 && t=/opt/toolchains/deps/taskflow-${TASKFLOW_VER} && mkdir -p "$t/src" \
 && curl -fsSL -o /tmp/tf.tgz https://github.com/taskflow/taskflow/archive/refs/tags/v${TASKFLOW_VER}.tar.gz \
 && echo "${TASKFLOW_SHA256}  /tmp/tf.tgz" | sha256sum -c - \
 && tar -xzf /tmp/tf.tgz --strip-components=1 -C "$t/src" && rm -f /tmp/tf.tgz \
 && chmod -R a+rX /opt/toolchains/deps

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
