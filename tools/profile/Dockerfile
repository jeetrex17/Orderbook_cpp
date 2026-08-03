# Valgrind does not run natively on macOS/ARM, so profiling happens in Linux.
# Build for the host architecture (linux/arm64 on Apple Silicon): forcing amd64
# would run under QEMU, which is both slow and a simulation of a different
# machine than the one the wall-clock numbers come from.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      valgrind \
      ca-certificates \
      git \
      python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
