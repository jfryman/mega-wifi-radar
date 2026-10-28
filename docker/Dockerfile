# mega_wifi_radar build image: m68k-elf toolchain + SGDK pinned and patched for MegaWiFi Rev.C.
# Build through scripts/md (it tags the image by the hash of docker/ and sgdk/), not by hand.

ARG DEBIAN=debian:bookworm-slim

# ---------------------------------------------------------------- toolchain
FROM ${DEBIAN} AS toolchain

ARG BINUTILS_VER=2.42
ARG BINUTILS_SHA256=f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800
ARG GCC_VER=13.2.0
ARG GCC_SHA256=e275e76442a6067341a27f04c5c6b83d8613144004c0413528863dc6b5c743da
ARG PREFIX=/opt/m68k

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl xz-utils texinfo \
        libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN curl -fsSL -o binutils.tar.xz https://ftpmirror.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz \
 && curl -fsSL -o gcc.tar.xz https://ftpmirror.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz \
 && echo "${BINUTILS_SHA256}  binutils.tar.xz" | sha256sum -c - \
 && echo "${GCC_SHA256}  gcc.tar.xz" | sha256sum -c - \
 && tar xf binutils.tar.xz && tar xf gcc.tar.xz && rm *.tar.xz

RUN mkdir build-binutils && cd build-binutils \
 && ../binutils-${BINUTILS_VER}/configure --target=m68k-elf --prefix=${PREFIX} \
        --with-cpu=m68000 --disable-nls --disable-werror --enable-plugins --enable-lto \
 && make -j"$(nproc)" && make install-strip

ENV PATH=${PREFIX}/bin:${PATH}

# C only, bare metal, single 68000 libgcc (no multilib).
RUN mkdir build-gcc && cd build-gcc \
 && ../gcc-${GCC_VER}/configure --target=m68k-elf --prefix=${PREFIX} \
        --with-cpu=m68000 --disable-multilib --enable-languages=c --enable-lto \
        --without-headers --with-newlib --disable-nls --disable-shared --disable-threads \
        --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
 && make -j"$(nproc)" all-gcc all-target-libgcc \
 && make install-strip-gcc install-target-libgcc

# ---------------------------------------------------------------- sgdk
FROM ${DEBIAN} AS sgdk

ARG SGDK_REPO=https://github.com/Stephane-D/SGDK
ARG SGDK_COMMIT=9a78a44143a867cd8c5053b95cd061a21f0b73c2

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git make openjdk-17-jre-headless \
        libgmp10 libmpfr6 libmpc3 zlib1g \
    && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /opt/m68k /opt/m68k
ENV PATH=/opt/m68k/bin:/sgdk/bin:${PATH} GDK=/sgdk

RUN git init -q /sgdk && cd /sgdk \
 && git remote add origin ${SGDK_REPO} \
 && git fetch -q --depth 1 origin ${SGDK_COMMIT} && git checkout -q FETCH_HEAD

# Rev.C patches. `git apply --check` makes a drifted pin fail loudly instead of half-applying.
COPY sgdk/patches /tmp/patches
RUN cd /sgdk && for p in /tmp/patches/*.patch; do git apply --check "$p" && git apply "$p"; done

# Native host tools (the repo ships Windows .exe builds only).
RUN cd /sgdk && for t in bintos sjasm xgmtool; do \
        cmake -S tools/$t -B /tmp/build/$t -DCMAKE_BUILD_TYPE=Release >/dev/null \
     && cmake --build /tmp/build/$t -j"$(nproc)" \
     && install -m755 /tmp/build/$t/$t bin/$t; \
    done \
 && make -C tools/convsym && install -m755 tools/convsym/build/convsym bin/convsym \
 && rm -rf /tmp/build bin/*.exe bin/*.dll

# Drop upstream's prebuilt (unpatched) libraries and rebuild from patched sources.
RUN cd /sgdk && rm -f lib/libmd.a lib/libmd_debug.a \
 && make -f makelib.gen release && make -f makelib.gen debug && rm -rf out

# Prove the image is Rev.C: patched headers compile the guard, and the lib contains the 16C550 driver.
COPY src/rev_c_guard.c /tmp/rev_c_guard.c
RUN m68k-elf-gcc -m68000 -DSGDK_GCC -I/sgdk/inc -I/sgdk/res -fsyntax-only /tmp/rev_c_guard.c \
 && m68k-elf-nm /sgdk/lib/libmd.a 2>/dev/null | grep -q ' T uart_init$' \
 && echo "SGDK ${SGDK_COMMIT} patched for MegaWiFi Rev.C" > /sgdk/REV_C_STAMP

WORKDIR /work
