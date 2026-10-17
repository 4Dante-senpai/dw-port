# DW1 Web Port toolchain. Extends the upstream dw_decomp image (repo-root Dockerfile)
# with the port toolchain: Clang/LLD, CMake, Ninja, emsdk and SDL3 (native + Emscripten).
# Build with tools/port/docker_build.sh.
ARG BASE_IMAGE=dw-build:latest
FROM ${BASE_IMAGE} AS toolchain

ARG EMSDK_VERSION=6.0.10
ARG SDL3_VERSION=3.4.16

USER root

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    curl \
    gcc-multilib \
    lld \
    ninja-build \
    p7zip-full \
    xvfb xauth libgl1-mesa-dri \
    libcapstone4 libavcodec60 libavformat60 libavutil58 libswresample4 \
    libcurl4t64 libuv1t64 libfreetype6 \
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

# Playwright, pinned (NFR-9), with Chromium and Firefox for the web tests
ARG PLAYWRIGHT_VERSION=1.64.0
ENV PLAYWRIGHT_BROWSERS_PATH=/opt/ms-playwright
RUN export PATH="$(dirname /opt/emsdk/node/*/bin/node):$PATH" && \
    npm install -g @playwright/test@${PLAYWRIGHT_VERSION} && \
    npx playwright install --with-deps chromium firefox && \
    ln -s "$(npm root -g)" /opt/node_modules && \
    ln -s /opt/node_modules/@playwright/test/cli.js /usr/local/bin/playwright && \
    chmod -R a+rX /opt/ms-playwright && \
    apt-get clean && rm -rf /var/lib/apt/lists/*
ENV NODE_PATH=/opt/node_modules

# PCSX-Redux reference emulator (story 3.1), built against the toolchain stage's SDL3.
FROM toolchain AS pcsx-redux
ARG PCSX_REDUX_SHA=2068784a552b598940191e8431585b7f96408b3f
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config cmake ninja-build ca-certificates \
    libcapstone-dev libfreetype-dev libavcodec-dev libavformat-dev libavutil-dev \
    libswresample-dev libcurl4-openssl-dev libuv1-dev zlib1g-dev libgl-dev \
    gcc-mipsel-linux-gnu g++-mipsel-linux-gnu && \
    rm -rf /var/lib/apt/lists/*
RUN git init /tmp/pcsx && cd /tmp/pcsx && \
    git remote add origin https://github.com/grumpycoders/pcsx-redux.git && \
    git fetch --depth 1 origin ${PCSX_REDUX_SHA} && git checkout --detach FETCH_HEAD && \
    SUBMODULE_RETRY_ATTEMPTS=3 bash .github/scripts/update-submodules.sh && \
    PKG_CONFIG_PATH=/opt/sdl3/native/lib/pkgconfig make -j2 CPPFLAGS_Release="-O1 -g0" && \
    make -C src/mips/openbios -j4 PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips && \
    strip pcsx-redux && mkdir -p /out/libexec /out/share/pcsx-redux/resources && \
    cp pcsx-redux /out/libexec/ && cp src/mips/openbios/openbios.bin /out/share/pcsx-redux/ && \
    cp LICENSE LICENSES.md /out/share/pcsx-redux/ && \
    cp src/mips/LICENSE /out/share/pcsx-redux/OpenBIOS-LICENSE && \
    printf '{"version":"dw1-reference","changeset":"%s","timestamp":%s}\n' \
        "${PCSX_REDUX_SHA}" "$(git show -s --format=%ct HEAD)" > /out/share/pcsx-redux/resources/version.json

FROM toolchain
# Only runtime artifacts cross the emulator build-stage boundary.
COPY --from=pcsx-redux /out/ /usr/local/
RUN ln -s /usr/local/libexec/pcsx-redux /usr/local/bin/pcsx-redux && ldconfig && \
    xvfb-run -a pcsx-redux -version

# Writable by any uid so tools/port/docker_run.sh can run as the host user
RUN chmod -R a+rwX /opt/emsdk/upstream/emscripten/cache /dw/.venv

USER ubuntu
WORKDIR /dw
