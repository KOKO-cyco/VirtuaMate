#!/usr/bin/env bash
# Build static ARM64 VRM deps into third_party/linux-aarch64/ (no apt dev packages on host).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/third_party/linux-aarch64"
BUILD="${ROOT}/.cache/third_party_aarch64_build"
TOOLCHAIN="${ROOT}/TuyaOpen/platform/tools/aarch64-none-linux-gnu-14.2-2024.10/bin"
PREFIX="${TOOLCHAIN}/aarch64-none-linux-gnu"

export PATH="${TOOLCHAIN}:${PATH}"
CC="${TOOLCHAIN}/aarch64-none-linux-gnu-gcc"
CXX="${TOOLCHAIN}/aarch64-none-linux-gnu-g++"
AR="${TOOLCHAIN}/aarch64-none-linux-gnu-ar"
RANLIB="${TOOLCHAIN}/aarch64-none-linux-gnu-ranlib"

ASSIMP_VER="5.4.3"
GLEW_VER="2.2.0"
SDL2_VER="2.30.0"

if [[ ! -x "${CC}" ]]; then
    echo "Cross compiler not found: ${CC}" >&2
    echo "Run a TuyaOpen build once so platform/tools is downloaded, or install TuyaOpen toolchain." >&2
    exit 1
fi

mkdir -p "${OUT}/assimp/lib" "${OUT}/assimp/include" \
         "${OUT}/glew/lib" "${OUT}/glew/include" \
         "${OUT}/sdl2/lib" "${OUT}/sdl2/include/SDL2" \
         "${BUILD}"

cmake_cross() {
    cmake -S "$1" -B "$2" \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER="${CC}" \
        -DCMAKE_CXX_COMPILER="${CXX}" \
        -DCMAKE_AR="${AR}" \
        -DCMAKE_RANLIB="${RANLIB}" \
        -DCMAKE_FIND_ROOT_PATH="${PREFIX}" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_BUILD_TYPE=Release \
        "${@:3}"
}

# --- assimp (static) ---
if [[ ! -f "${OUT}/assimp/lib/libassimp.a" ]]; then
    echo "==> Building assimp ${ASSIMP_VER} for aarch64..."
    ASSIMP_SRC="${BUILD}/assimp-${ASSIMP_VER}"
    if [[ ! -d "${ASSIMP_SRC}" ]]; then
        curl -fsSL "https://github.com/assimp/assimp/archive/v${ASSIMP_VER}.tar.gz" \
            | tar -xz -C "${BUILD}"
    fi
    cmake_cross "${ASSIMP_SRC}" "${BUILD}/assimp-build" \
        -DASSIMP_BUILD_TESTS=OFF \
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF \
        -DASSIMP_INSTALL=OFF \
        -DASSIMP_BUILD_ZLIB=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DASSIMP_NO_EXPORT=ON
    cmake --build "${BUILD}/assimp-build" -j"$(nproc)"
    cp "${BUILD}/assimp-build/lib/libassimp.a" "${OUT}/assimp/lib/"
    cp -r "${ASSIMP_SRC}/include/assimp" "${OUT}/assimp/include/"
    cp "${BUILD}/assimp-build/include/assimp/config.h" "${OUT}/assimp/include/assimp/"
    cp "${BUILD}/assimp-build/include/assimp/revision.h" "${OUT}/assimp/include/assimp/"
fi

# --- GLEW (static via upstream Makefile; X11 headers from arm64 .deb for compile only) ---
if [[ ! -f "${OUT}/glew/lib/libGLEW.a" ]]; then
    echo "==> Building GLEW ${GLEW_VER} for aarch64..."
    GLEW_SRC="${BUILD}/glew-${GLEW_VER}"
    if [[ ! -d "${GLEW_SRC}" ]]; then
        curl -fsSL "https://github.com/nigels-com/glew/releases/download/glew-${GLEW_VER}/glew-${GLEW_VER}.tgz" \
            | tar -xz -C "${BUILD}"
    fi
  if [[ -f /usr/include/X11/X.h ]]; then
      GLEW_CFLAGS_EXTRA="-I/usr/include"
  else
      X11_HDR="${BUILD}/x11-headers"
      rm -rf "${X11_HDR}"
      mkdir -p "${X11_HDR}"
      XORGPROTO_DEB="$(apt-cache show xorgproto 2>/dev/null | awk '/^Filename:/{print $2; exit}')"
      if [[ -z "${XORGPROTO_DEB}" ]]; then
          echo "Need X11 headers to build GLEW. Install: sudo apt-get install -y xorgproto libx11-dev" >&2
          exit 1
      fi
      curl -fsSL -o "${BUILD}/xorgproto.deb" "https://archive.ubuntu.com/ubuntu/${XORGPROTO_DEB}"
      dpkg-deb -x "${BUILD}/xorgproto.deb" "${X11_HDR}"
      GLEW_CFLAGS_EXTRA="-I${X11_HDR}/usr/include"
  fi
    make -C "${GLEW_SRC}" -f Makefile \
        SYSTEM=linux \
        CC="${CC}" \
        AR="${AR}" \
        M_ARCH=aarch64 \
        STRIP= \
        CFLAGS.EXTRA="${GLEW_CFLAGS_EXTRA}" \
        glew.lib.static
    cp "${GLEW_SRC}/lib/libGLEW.a" "${OUT}/glew/lib/"
    mkdir -p "${OUT}/glew/include/GL"
    cp "${GLEW_SRC}/include/GL/"*.h "${OUT}/glew/include/GL/"
fi

# --- SDL2: headers (amd64 dev .deb) + arm64 shared lib (libsdl2-classic) ---
SDL2_DEB_VER="2.32.10+dfsg-6"
if [[ ! -f "${OUT}/sdl2/lib/libSDL2.so" ]]; then
    echo "==> Fetching SDL2 ${SDL2_DEB_VER} for cross-link..."
    SDL2_DEB_DIR="${BUILD}/sdl2-debs"
    mkdir -p "${SDL2_DEB_DIR}"
    SDL2_DEV_DEB="libsdl2-dev_${SDL2_DEB_VER}_amd64.deb"
    SDL2_LIB_DEB="libsdl2-classic_${SDL2_DEB_VER}_arm64.deb"
    if [[ ! -f "${SDL2_DEB_DIR}/${SDL2_DEV_DEB}" ]]; then
        curl -fsSL -o "${SDL2_DEB_DIR}/${SDL2_DEV_DEB}" \
            "https://archive.ubuntu.com/ubuntu/pool/universe/libs/libsdl2/${SDL2_DEV_DEB}"
    fi
    if [[ ! -f "${SDL2_DEB_DIR}/${SDL2_LIB_DEB}" ]]; then
        curl -fsSL -o "${SDL2_DEB_DIR}/${SDL2_LIB_DEB}" \
            "https://ports.ubuntu.com/pool/main/libs/libsdl2/${SDL2_LIB_DEB}"
    fi
    SDL2_EXTRACT="${BUILD}/sdl2-extract"
    rm -rf "${SDL2_EXTRACT}"
    mkdir -p "${SDL2_EXTRACT}"
    dpkg-deb -x "${SDL2_DEB_DIR}/${SDL2_DEV_DEB}" "${SDL2_EXTRACT}"
    dpkg-deb -x "${SDL2_DEB_DIR}/${SDL2_LIB_DEB}" "${SDL2_EXTRACT}"
    cp -a "${SDL2_EXTRACT}/usr/lib/aarch64-linux-gnu/sdl2-classic/libSDL2"*.so* "${OUT}/sdl2/lib/"
    ln -sf libSDL2-2.0.so.0 "${OUT}/sdl2/lib/libSDL2.so"
    cp -r "${SDL2_EXTRACT}/usr/include/SDL2/"* "${OUT}/sdl2/include/SDL2/"
fi

echo "Done. ARM64 VRM deps installed under ${OUT}"
ls -la "${OUT}/assimp/lib" "${OUT}/glew/lib" "${OUT}/sdl2/lib"
