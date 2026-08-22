FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        can-utils \
        clang-tidy \
        cmake \
        gcc-arm-none-eabi \
        gdb-multiarch \
        libstdc++-arm-none-eabi-newlib \
        make \
        openocd \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
