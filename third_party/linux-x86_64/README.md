# x86_64 VRM dependencies (native Ubuntu build)

Prebuilt libraries for **Ubuntu x86_64 native** builds (`Ubuntu.config`).
CMake uses these when the compiler is `gcc`/`cc`/`clang` instead of host `pkg-config`.

| Package | Contents |
|---------|----------|
| `assimp/` | `libassimp.a` + headers |
| `glew/`   | `libGLEW.a` + GL headers |
| `sdl2/`   | `libSDL2-2.0.so.*` + headers |
| `mesa/`   | `GL/gl.h` etc. (compile-only headers) |

**Runtime on Ubuntu:** OpenGL / X11 from system (`libGL`, `libX11`). SDL2 is loaded from
this directory via `-Wl,-rpath` baked in at link time.

**Refresh (maintainers):**

```bash
./scripts/build_third_party_linux_x86_64.sh
```

Build host needs `cmake`, `curl`, `make`, `dpkg-deb`; optional X11 headers only when
rebuilding GLEW from source.
