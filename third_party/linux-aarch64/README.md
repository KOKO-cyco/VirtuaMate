# ARM64 VRM dependencies (cross-compile)

Prebuilt libraries for **Ubuntu x86_64 → Raspberry Pi / DshanPi ARM64** cross-builds.
When `CMAKE_C_COMPILER` is `aarch64-none-linux-gnu-gcc`, CMake uses these instead of
host `pkg-config` (sdl2 / glew / assimp).

| Package | Contents |
|---------|----------|
| `assimp/` | `libassimp.a` + headers (incl. generated `config.h`) |
| `glew/`   | `libGLEW.a` + GL headers |
| `sdl2/`   | `libSDL2-2.0.so.*` + headers (Ubuntu arm64 classic) |

**Runtime on Pi:** OpenGL / GLX / X11 come from the device (`libGL`, `libSDL2`, etc.).

**Refresh (maintainers):**

```bash
./scripts/build_third_party_linux_aarch64.sh
```

Requires TuyaOpen cross toolchain (`platform/tools/aarch64-none-linux-gnu-*`) and network.
Host needs `cmake`, `curl`, `make`; optional `xorgproto` / `/usr/include/X11` for GLEW build.
