##
# @file vrm_linux_deps.cmake
# @brief VRM renderer dependencies for LINUX (bundled prebuilts vs pkg-config fallback)
#/

if(NOT PLATFORM_NAME STREQUAL "LINUX")
    return()
endif()

get_filename_component(_VRM_C_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)

set(_VRM_BUNDLED_ROOT "")
if(_VRM_C_COMPILER_NAME MATCHES "^aarch64-none-linux-gnu-gcc$")
    set(_VRM_BUNDLED_ROOT "${APP_PATH}/third_party/linux-aarch64")
elseif(_VRM_C_COMPILER_NAME MATCHES "^(gcc|cc|clang)$")
    set(_VRM_BUNDLED_ROOT "${APP_PATH}/third_party/linux-x86_64")
endif()

if(_VRM_BUNDLED_ROOT)
    set(_VRM_ASSIMP_LIB "${_VRM_BUNDLED_ROOT}/assimp/lib/libassimp.a")
    set(_VRM_GLEW_LIB   "${_VRM_BUNDLED_ROOT}/glew/lib/libGLEW.a")
    set(_VRM_SDL2_LIB   "${_VRM_BUNDLED_ROOT}/sdl2/lib/libSDL2.so")

    if(NOT EXISTS "${_VRM_ASSIMP_LIB}" OR NOT EXISTS "${_VRM_GLEW_LIB}" OR NOT EXISTS "${_VRM_SDL2_LIB}")
        if(_VRM_C_COMPILER_NAME MATCHES "^aarch64-none-linux-gnu-gcc$")
            set(_VRM_BUILD_SCRIPT "./scripts/build_third_party_linux_aarch64.sh")
        else()
            set(_VRM_BUILD_SCRIPT "./scripts/build_third_party_linux_x86_64.sh")
        endif()
        message(FATAL_ERROR
            "Missing bundled VRM deps under ${_VRM_BUNDLED_ROOT}. "
            "Run: ${_VRM_BUILD_SCRIPT}")
    endif()

    list(APPEND APP_INC
        "${_VRM_BUNDLED_ROOT}/assimp/include"
        "${_VRM_BUNDLED_ROOT}/glew/include"
        "${_VRM_BUNDLED_ROOT}/sdl2/include/SDL2"
    )

    if(_VRM_C_COMPILER_NAME MATCHES "^aarch64-none-linux-gnu-gcc$")
        list(APPEND APP_INC /usr/include)
    else()
        list(APPEND APP_INC "${_VRM_BUNDLED_ROOT}/mesa/include")
    endif()

    set(VRM_RENDER_COMPILE_FLAGS -DGLEW_STATIC -DGLEW_NO_GLU)

    get_filename_component(_VRM_SDL2_LIB_DIR "${_VRM_SDL2_LIB}" DIRECTORY)

    set(VRM_RENDER_LINK_FLAGS
        "${_VRM_ASSIMP_LIB}"
        "${_VRM_GLEW_LIB}"
        "${_VRM_SDL2_LIB}"
        "-Wl,-rpath,${_VRM_SDL2_LIB_DIR}"
        "-Wl,--allow-shlib-undefined"
        "-Wl,--unresolved-symbols=ignore-all"
        "-ldl"
        "-lpthread"
        "-lm"
    )
    string(REPLACE ";" " " VRM_RENDER_LINK_FLAGS "${VRM_RENDER_LINK_FLAGS}")

    message(STATUS "[VRM] Using bundled deps from ${_VRM_BUNDLED_ROOT}")
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
