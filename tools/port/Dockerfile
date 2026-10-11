# DW1 Web Port toolchain. Extends the upstream dw_decomp image (repo-root Dockerfile)
# with the port toolchain: Clang/LLD, CMake, Ninja, emsdk and SDL3 (native + Emscripten).
# Build with tools/port/docker_build.sh.
ARG BASE_IMAGE=dw-build:latest
FROM ${BASE_IMAGE}

ARG EMSDK_VERSION=6.0.10
ARG SDL3_VERSION=3.4.16

USER root

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    curl \
    lld \
    ninja-build \
    p7zip-full \
    pkg-config \
    xz-utils \
    libasound2-dev \
    libpulse-dev \
    libudev-dev \
    libdbus-1-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxss-dev \
    libxtst-dev \
    libxinerama-dev \
    libxfixes-dev \
    libdrm-dev \
    libgbm-dev \
    libdecor-0-dev \
    libxkbcommon-dev \
    libwayland-dev \
    libegl-dev \
    libgl-dev \
    && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# emsdk, pinned
RUN git clone --depth 1 --branch ${EMSDK_VERSION} https://github.com/emscripten-core/emsdk.git /opt/emsdk && \
    cd /opt/emsdk && \
    ./emsdk install ${EMSDK_VERSION} && \
    ./emsdk activate ${EMSDK_VERSION} && \
    ln -s /opt/emsdk/node/*/bin/node /usr/local/bin/node && \
    rm -rf /opt/emsdk/downloads/*

ENV EMSDK=/opt/emsdk
ENV EM_CACHE=/opt/emsdk/upstream/emscripten/cache
ENV EM_CONFIG=/opt/emsdk/.emscripten

ENV PATH="/opt/emsdk/upstream/emscripten:$PATH"

# SDL3, pinned, built from the same source for native (Linux x64) and Emscripten
RUN git clone --depth 1 --branch release-${SDL3_VERSION} https://github.com/libsdl-org/SDL.git /opt/sdl3/src && \
    cmake -S /opt/sdl3/src -B /tmp/sdl3-native -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3/native \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF && \
    cmake --build /tmp/sdl3-native && \
    cmake --install /tmp/sdl3-native && \
    rm -rf /tmp/sdl3-native

RUN emcmake cmake -S /opt/sdl3/src -B /tmp/sdl3-web -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sdl3/web \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_SHARED=OFF -DSDL_STATIC=ON && \
    cmake --build /tmp/sdl3-web && \
    cmake --install /tmp/sdl3-web && \
    rm -rf /tmp/sdl3-web /opt/sdl3/src

ENV SDL3_NATIVE_PREFIX=/opt/sdl3/native
ENV SDL3_WEB_PREFIX=/opt/sdl3/web

# Writable by any uid so tools/port/docker_run.sh can run as the host user
RUN chmod -R a+rwX /opt/emsdk/upstream/emscripten/cache /dw/.venv

USER ubuntu
WORKDIR /dw
