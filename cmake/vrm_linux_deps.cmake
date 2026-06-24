##
# @file vrm_linux_deps.cmake
# @brief VRM renderer dependencies for LINUX (native pkg-config vs bundled ARM64)
#/

if(NOT PLATFORM_NAME STREQUAL "LINUX")
    return()
endif()

get_filename_component(_VRM_C_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)
set(_VRM_USE_BUNDLED_ARM64 OFF)
if(_VRM_C_COMPILER_NAME MATCHES "^aarch64-none-linux-gnu-gcc$")
    set(_VRM_USE_BUNDLED_ARM64 ON)
endif()

if(_VRM_USE_BUNDLED_ARM64)
    set(VRM_ARM64_ROOT "${APP_PATH}/third_party/linux-aarch64")

    set(_VRM_ASSIMP_LIB "${VRM_ARM64_ROOT}/assimp/lib/libassimp.a")
    set(_VRM_GLEW_LIB   "${VRM_ARM64_ROOT}/glew/lib/libGLEW.a")
    set(_VRM_SDL2_LIB   "${VRM_ARM64_ROOT}/sdl2/lib/libSDL2.so")

    if(NOT EXISTS "${_VRM_ASSIMP_LIB}")
        message(FATAL_ERROR
            "Missing ${_VRM_ASSIMP_LIB}. "
            "Run: ./scripts/build_third_party_linux_aarch64.sh")
    endif()
    if(NOT EXISTS "${_VRM_GLEW_LIB}")
        message(FATAL_ERROR
            "Missing ${_VRM_GLEW_LIB}. "
            "Run: ./scripts/build_third_party_linux_aarch64.sh")
    endif()
    if(NOT EXISTS "${_VRM_SDL2_LIB}")
        message(FATAL_ERROR
            "Missing ${_VRM_SDL2_LIB}. "
            "Run: ./scripts/build_third_party_linux_aarch64.sh")
    endif()

    list(APPEND APP_INC
        "${VRM_ARM64_ROOT}/assimp/include"
        "${VRM_ARM64_ROOT}/glew/include"
        "${VRM_ARM64_ROOT}/sdl2/include/SDL2"
    )

    list(APPEND APP_INC /usr/include)

    set(VRM_RENDER_COMPILE_FLAGS -DGLEW_STATIC -DGLEW_NO_GLU)

    # GL/X11 resolve on the Pi at runtime; allow undefined shared refs when cross-linking.
    set(VRM_RENDER_LINK_FLAGS
        "${_VRM_ASSIMP_LIB}"
        "${_VRM_GLEW_LIB}"
        "${_VRM_SDL2_LIB}"
        "-Wl,--allow-shlib-undefined"
        "-Wl,--unresolved-symbols=ignore-all"
        "-ldl"
        "-lpthread"
        "-lm"
    )
    string(REPLACE ";" " " VRM_RENDER_LINK_FLAGS "${VRM_RENDER_LINK_FLAGS}")

    message(STATUS "[VRM] Using bundled ARM64 deps from ${VRM_ARM64_ROOT}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VRM_RENDER_DEPS REQUIRED IMPORTED_TARGET sdl2 glew assimp)

    list(APPEND APP_INC
        ${VRM_RENDER_DEPS_INCLUDE_DIRS}
    )

    string(REPLACE ";" " " VRM_RENDER_LINK_FLAGS "${VRM_RENDER_DEPS_LDFLAGS}")
    if(VRM_RENDER_DEPS_CFLAGS_OTHER)
        string(REPLACE ";" " " _VRM_CFLAGS_OTHER "${VRM_RENDER_DEPS_CFLAGS_OTHER}")
        set(VRM_RENDER_COMPILE_FLAGS "${_VRM_CFLAGS_OTHER}")
    endif()

    message(STATUS "[VRM] Using native pkg-config deps (sdl2 glew assimp)")
endif()

set(PLATFORM_NEED_LIBS "${PLATFORM_NEED_LIBS} ${VRM_RENDER_LINK_FLAGS}")
