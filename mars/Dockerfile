# mega_wifi_radar 32X build image: m68k-elf (68000) + sh-elf (SH-2) toolchains and the 32X boot code.
# Build through scripts/mars (it tags the image by the hash of mars/Dockerfile + sgdk/patches), not by hand.
# Separate from docker/Dockerfile on purpose: the Genesis image and its hash stay untouched. Build context: repo root.

ARG DEBIAN=debian:bookworm-slim

# ---------------------------------------------------------------- toolchains
FROM ${DEBIAN} AS toolchain

ARG BINUTILS_VER=2.42
ARG BINUTILS_SHA256=f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800
ARG GCC_VER=13.2.0
ARG GCC_SHA256=e275e76442a6067341a27f04c5c6b83d8613144004c0413528863dc6b5c743da

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

# 68000 side: same recipe as docker/Dockerfile (C only, bare metal, 68000 libgcc, no multilib).
RUN mkdir b68 && cd b68 \
 && ../binutils-${BINUTILS_VER}/configure --target=m68k-elf --prefix=/opt/m68k \
        --with-cpu=m68000 --disable-nls --disable-werror \
 && make -j"$(nproc)" && make install-strip
RUN mkdir g68 && cd g68 && PATH=/opt/m68k/bin:$PATH \
    ../gcc-${GCC_VER}/configure --target=m68k-elf --prefix=/opt/m68k \
        --with-cpu=m68000 --disable-multilib --enable-languages=c \
        --without-headers --with-newlib --disable-nls --disable-shared --disable-threads \
        --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
 && PATH=/opt/m68k/bin:$PATH make -j"$(nproc)" all-gcc all-target-libgcc \
 && PATH=/opt/m68k/bin:$PATH make install-strip-gcc install-target-libgcc

# SH-2 side: big-endian SH-2 (-m2 -mb) only, so libgcc is the right one without multilib flags.
RUN mkdir bsh && cd bsh \
 && ../binutils-${BINUTILS_VER}/configure --target=sh-elf --prefix=/opt/sh \
        --disable-nls --disable-werror \
 && make -j"$(nproc)" && make install-strip
RUN mkdir gsh && cd gsh && PATH=/opt/sh/bin:$PATH \
    ../gcc-${GCC_VER}/configure --target=sh-elf --prefix=/opt/sh \
        --with-cpu=m2 --with-endian=big --disable-multilib --enable-languages=c \
        --without-headers --with-newlib --disable-nls --disable-shared --disable-threads \
        --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
 && PATH=/opt/sh/bin:$PATH make -j"$(nproc)" all-gcc all-target-libgcc \
 && PATH=/opt/sh/bin:$PATH make install-strip-gcc install-target-libgcc

# ---------------------------------------------------------------- 32X boot code
# The 68000 security/boot code every 32X cart carries at $3F0 is Sega's; we don't keep a copy in the repo.
# It is taken from marsdev (MIT, andwn/marsdev) at a pinned commit and checked by hash.
FROM ${DEBIAN} AS marsboot

ARG MARSDEV_REPO=https://github.com/andwn/marsdev
ARG MARSDEV_COMMIT=3318d3f39823154b24ce48bfa5d8fe6e3f6cde3f
ARG MARS_BLOB_SHA256=ff47486672ce72191c7532c205ea95732a62899d81659b1be0407ef9ab6b47fa

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates git gawk \
    && rm -rf /var/lib/apt/lists/*
RUN git init -q /marsdev && cd /marsdev \
 && git remote add origin ${MARSDEV_REPO} \
 && git fetch -q --depth 1 origin ${MARSDEV_COMMIT} && git checkout -q FETCH_HEAD \
 && mkdir -p /opt/mars/boot \
 && awk '/^_mars_startup_blob:/{f=1;next} f&&/^[[:space:]]*$/{exit} f' \
        examples/32x-skeleton/md_src/md_start.s > /opt/mars/boot/mars_security.inc \
 && echo "${MARS_BLOB_SHA256}  /opt/mars/boot/mars_security.inc" | sha256sum -c - \
 && cp LICENSE /opt/mars/boot/MARSDEV_LICENSE \
 && echo "marsdev ${MARSDEV_COMMIT}" > /opt/mars/boot/SOURCE

# ---------------------------------------------------------------- SGDK's MegaWiFi driver
# The 32X ROM's 68000 runs SGDK's comm/lsd/megawifi sources without the rest of SGDK (lode/mars/summary.md).
# Same pin and the same Rev.C patches as docker/Dockerfile: keep SGDK_COMMIT in step with it.
FROM ${DEBIAN} AS sgdkmw

ARG SGDK_REPO=https://github.com/Stephane-D/SGDK
ARG SGDK_COMMIT=9a78a44143a867cd8c5053b95cd061a21f0b73c2

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
RUN git init -q /sgdk && cd /sgdk \
 && git remote add origin ${SGDK_REPO} \
 && git fetch -q --depth 1 origin ${SGDK_COMMIT} && git checkout -q FETCH_HEAD
COPY sgdk/patches /tmp/patches
RUN cd /sgdk && for p in /tmp/patches/*.patch; do git apply --check "$p" && git apply "$p"; done \
 && mkdir -p /opt/mars/mw/src /opt/mars/mw/inc/ext/mw \
 && cp src/ext/mw/comm.c src/ext/mw/lsd.c src/ext/mw/megawifi.c /opt/mars/mw/src/ \
 && cp inc/ext/mw/comm.h inc/ext/mw/lsd.h inc/ext/mw/megawifi.h inc/ext/mw/mw-msg.h /opt/mars/mw/inc/ext/mw/ \
 && cp license.txt /opt/mars/mw/SGDK_LICENSE 2>/dev/null || cp LICENSE* /opt/mars/mw/ \
 && echo "SGDK ${SGDK_COMMIT} + sgdk/patches" > /opt/mars/mw/SOURCE

# ---------------------------------------------------------------- final
FROM ${DEBIAN}

RUN apt-get update && apt-get install -y --no-install-recommends \
        make libgmp10 libmpfr6 libmpc3 zlib1g coreutils \
    && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /opt/m68k /opt/m68k
COPY --from=toolchain /opt/sh /opt/sh
COPY --from=marsboot /opt/mars /opt/mars
COPY --from=sgdkmw /opt/mars/mw /opt/mars/mw
ENV PATH=/opt/m68k/bin:/opt/sh/bin:${PATH} MARS_BOOT=/opt/mars/boot MARS_MW=/opt/mars/mw

# Prove both compilers produce code for the right CPUs.
RUN printf 'int f(int a,int b){return a/b;}\n' > /tmp/t.c \
 && m68k-elf-gcc -m68000 -O2 -c /tmp/t.c -o /tmp/t68.o \
 && sh-elf-gcc -m2 -mb -O2 -c /tmp/t.c -o /tmp/tsh.o \
 && sh-elf-gcc -m2 -mb -print-libgcc-file-name | grep -q libgcc.a \
 && m68k-elf-gcc -m68000 -print-libgcc-file-name | grep -q libgcc.a \
 && rm /tmp/t*

WORKDIR /work
