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
    PKG_CONFIG_PATH=/opt/sdl3/native/lib/pkgconfig make -j3 CPPFLAGS_Release="-O1 -g0" && \
    make -C src/mips/openbios -j4 PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips && \
    strip pcsx-redux && mkdir -p /out/libexec /out/share/pcsx-redux/resources && \
    cp pcsx-redux /out/libexec/ && cp src/mips/openbios/openbios.bin /out/share/pcsx-redux/ && \
    cp LICENSE LICENSES.md /out/share/pcsx-redux/ && \
    cp src/mips/LICENSE /out/share/pcsx-redux/OpenBIOS-LICENSE && \
    printf '{"version":"dw1-reference","changeset":"%s","timestamp":%s}\n' \
        "${PCSX_REDUX_SHA}" "$(git show -s --format=%ct HEAD)" > /out/share/pcsx-redux/resources/version.json

# Capture the mixer's PCM before SDL queue drops or callback silence insertion.
# Apply against the pinned source, failing closed on any changed patch context.
COPY tools/port/reference/pcsx-redux-pcm-tap.patch /tmp/dw1-pcm-tap.patch
RUN cd /tmp/pcsx && git apply --check /tmp/dw1-pcm-tap.patch && \
    git apply /tmp/dw1-pcm-tap.patch && \
    PKG_CONFIG_PATH=/opt/sdl3/native/lib/pkgconfig make -j3 CPPFLAGS_Release="-O1 -g0" && \
    strip pcsx-redux && cp pcsx-redux /out/libexec/

# Match the measured SCPH-1001 rand startup seed (206 calls before New Game).
# Keep this after the expensive emulator layer; rebuild only OpenBIOS.
RUN cd /tmp/pcsx && \
    sed -i 's/^static uint32_t s_currentSeed;$/static uint32_t s_currentSeed = 0x24040001;/' src/mips/openbios/kernel/libcmisc.c && \
    (grep -q '^static uint32_t s_currentSeed = 0x24040001;$' src/mips/openbios/kernel/libcmisc.c || \
        { echo 'OpenBIOS rand seed patch failed: s_currentSeed declaration changed' >&2; exit 1; }) && \
    make -C src/mips/openbios -j4 PREFIX=mipsel-linux-gnu FORMAT=elf32-tradlittlemips && \
    cp src/mips/openbios/openbios.bin /out/share/pcsx-redux/

FROM toolchain
# Only runtime artifacts cross the emulator build-stage boundary.
COPY --from=pcsx-redux /out/ /usr/local/
RUN ln -s /usr/local/libexec/pcsx-redux /usr/local/bin/pcsx-redux && ldconfig && \
    xvfb-run -a pcsx-redux -version

# SDL3 i686 for the windowed native preset (story 3.9, AD-3), with X11, Wayland and PulseAudio so
# the window and audio reach WSLg. Last, so the emulator layers above keep their cache.
# Stage ARGs do not cross FROM: same pin as the toolchain stage's SDL3_VERSION.
# libglib2.0-dev:i386 accepts any python3; pin amd64 or apt swaps the venv's interpreter to i386.
ARG SDL3_VERSION=3.4.16
RUN dpkg --add-architecture i386 && apt-get update && \
    apt-get install -y --no-install-recommends \
    libasound2-dev:i386 libpulse-dev:i386 libudev-dev:i386 libdbus-1-dev:i386 \
    libx11-dev:i386 libxext-dev:i386 libxrandr-dev:i386 libxcursor-dev:i386 libxi-dev:i386 \
    libxss-dev:i386 libxtst-dev:i386 libxinerama-dev:i386 libxfixes-dev:i386 \
    libdrm-dev:i386 libgbm-dev:i386 libdecor-0-dev:i386 libxkbcommon-dev:i386 \
    libwayland-dev:i386 libegl-dev:i386 libgl-dev:i386 libgl1-mesa-dri:i386 \
    python3:amd64 python3-minimal:amd64 && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    python3 -c 'import struct; assert struct.calcsize("P") == 8, "python3 must stay amd64"' && \
    git clone --depth 1 --branch release-${SDL3_VERSION} https://github.com/libsdl-org/SDL.git /tmp/sdl3-src && \
    PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    cmake -S /tmp/sdl3-src -B /tmp/sdl3-native32 -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_LIBRARY_ARCHITECTURE=i386-linux-gnu \
        -DCMAKE_INSTALL_PREFIX=/opt/sdl3/native32 -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF && \
    cmake --build /tmp/sdl3-native32 && \
    cmake --install /tmp/sdl3-native32 && \
    test -f /opt/sdl3/native32/lib/cmake/SDL3/SDL3Config.cmake && \
    rm -rf /tmp/sdl3-native32 /tmp/sdl3-src
ENV SDL3_NATIVE32_PREFIX=/opt/sdl3/native32

# Writable by any uid so tools/port/docker_run.sh can run as the host user
RUN chmod -R a+rwX /opt/emsdk/upstream/emscripten/cache /dw/.venv

USER ubuntu
WORKDIR /dw
