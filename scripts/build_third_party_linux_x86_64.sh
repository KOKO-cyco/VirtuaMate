#!/usr/bin/env bash
# Build x86_64 VRM deps into third_party/linux-x86_64/ (no apt dev packages on host).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/third_party/linux-x86_64"
BUILD="${ROOT}/.cache/third_party_x86_64_build"

CC="${CC:-gcc}"
CXX="${CXX:-g++}"
AR="${AR:-ar}"
RANLIB="${RANLIB:-ranlib}"

ASSIMP_VER="5.4.3"
GLEW_VER="2.2.0"

fetch_ubuntu_deb() {
    local pkg="$1"
    local dest="$2"
    local deb_path
    deb_path="$(apt-cache show "${pkg}" 2>/dev/null | awk '/^Filename:/{print $2; exit}')"
    if [[ -z "${deb_path}" ]]; then
        echo "Cannot resolve Ubuntu package: ${pkg}" >&2
        exit 1
    fi
    curl -fsSL -o "${dest}" "https://archive.ubuntu.com/ubuntu/${deb_path}"
}

mkdir -p "${OUT}/assimp/lib" "${OUT}/assimp/include" \
         "${OUT}/glew/lib" "${OUT}/glew/include" \
         "${OUT}/sdl2/lib" "${OUT}/sdl2/include/SDL2" \
         "${OUT}/mesa/include" \
         "${BUILD}"

# --- assimp (static) ---
if [[ ! -f "${OUT}/assimp/lib/libassimp.a" ]]; then
    echo "==> Building assimp ${ASSIMP_VER} for x86_64..."
    ASSIMP_SRC="${BUILD}/assimp-${ASSIMP_VER}"
    if [[ ! -d "${ASSIMP_SRC}" ]]; then
        curl -fsSL "https://github.com/assimp/assimp/archive/v${ASSIMP_VER}.tar.gz" \
            | tar -xz -C "${BUILD}"
    fi
    cmake -S "${ASSIMP_SRC}" -B "${BUILD}/assimp-build" \
        -DCMAKE_C_COMPILER="${CC}" \
        -DCMAKE_CXX_COMPILER="${CXX}" \
        -DCMAKE_BUILD_TYPE=Release \
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

# --- GLEW (static via upstream Makefile) ---
if [[ ! -f "${OUT}/glew/lib/libGLEW.a" ]]; then
    echo "==> Building GLEW ${GLEW_VER} for x86_64..."
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
        STRIP= \
        CFLAGS.EXTRA="${GLEW_CFLAGS_EXTRA}" \
        glew.lib.static
    cp "${GLEW_SRC}/lib/libGLEW.a" "${OUT}/glew/lib/"
    mkdir -p "${OUT}/glew/include/GL"
    cp "${GLEW_SRC}/include/GL/"*.h "${OUT}/glew/include/GL/"
fi

# --- SDL2: headers + amd64 shared lib from Ubuntu debs ---
if [[ ! -f "${OUT}/sdl2/lib/libSDL2.so" ]]; then
    echo "==> Fetching SDL2 for x86_64..."
    SDL2_DEB_DIR="${BUILD}/sdl2-debs"
    mkdir -p "${SDL2_DEB_DIR}"
    fetch_ubuntu_deb "libsdl2-dev:amd64" "${SDL2_DEB_DIR}/libsdl2-dev.deb"
    if apt-cache show libsdl2-classic:amd64 &>/dev/null; then
        fetch_ubuntu_deb "libsdl2-classic:amd64" "${SDL2_DEB_DIR}/libsdl2-runtime.deb"
        SDL2_LIB_SUBDIR="usr/lib/x86_64-linux-gnu/sdl2-classic"
    else
        fetch_ubuntu_deb "libsdl2-2.0-0:amd64" "${SDL2_DEB_DIR}/libsdl2-runtime.deb"
        SDL2_LIB_SUBDIR="usr/lib/x86_64-linux-gnu"
    fi
    SDL2_EXTRACT="${BUILD}/sdl2-extract"
    rm -rf "${SDL2_EXTRACT}"
    mkdir -p "${SDL2_EXTRACT}"
    dpkg-deb -x "${SDL2_DEB_DIR}/libsdl2-dev.deb" "${SDL2_EXTRACT}"
    dpkg-deb -x "${SDL2_DEB_DIR}/libsdl2-runtime.deb" "${SDL2_EXTRACT}"
    cp -a "${SDL2_EXTRACT}/${SDL2_LIB_SUBDIR}/libSDL2"*.so* "${OUT}/sdl2/lib/"
    ln -sf libSDL2-2.0.so.0 "${OUT}/sdl2/lib/libSDL2.so"
    cp -r "${SDL2_EXTRACT}/usr/include/SDL2/"* "${OUT}/sdl2/include/SDL2/"
fi

# --- Mesa GL headers (compile-only; no libgl-dev on blank build host) ---
if [[ ! -f "${OUT}/mesa/include/GL/gl.h" ]]; then
    echo "==> Fetching Mesa GL headers for x86_64 compile..."
    MESA_DEB_DIR="${BUILD}/mesa-debs"
    mkdir -p "${MESA_DEB_DIR}"
    if [[ ! -f "${MESA_DEB_DIR}/libgl-dev.deb" ]]; then
        fetch_ubuntu_deb "libgl-dev:amd64" "${MESA_DEB_DIR}/libgl-dev.deb"
    fi
    MESA_EXTRACT="${BUILD}/mesa-extract"
    rm -rf "${MESA_EXTRACT}"
    mkdir -p "${MESA_EXTRACT}"
    dpkg-deb -x "${MESA_DEB_DIR}/libgl-dev.deb" "${MESA_EXTRACT}"
    cp -r "${MESA_EXTRACT}/usr/include/GL" "${OUT}/mesa/include/"
fi

echo "Done. x86_64 VRM deps installed under ${OUT}"
ls -la "${OUT}/assimp/lib" "${OUT}/glew/lib" "${OUT}/sdl2/lib"
