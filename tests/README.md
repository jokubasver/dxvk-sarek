# HDR lightmap format regression

On Mali devices without filtering for `R16G16B16A16_UNORM`, the native D3D9
backend stores `A16B16G16R16` textures as fp16 and converts uploads. A subsequent
lock must convert GPU readback back to UNORM16 before returning it to the
application. Source can lock an entire lightmap atlas and rewrite only a few
surfaces, so returning raw fp16 bits corrupts all the untouched texels on the
next upload. Conversion writes must also be flushed for read-only locks before
imported host pages can be reused for another GPU write.

## CPU check

From the repository root:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/d3d9_unorm16.cpp -o /tmp/d3d9-unorm16
/tmp/d3d9-unorm16
```

This checks all 65,536 fp16 bit patterns against an independent floating-point
decoder, all 65,536 UNORM16 round trips, and 100 repeated partial atlas updates.
The maximum UNORM16 round-trip error is 16/65535, from fp16 quantization;
untouched GPU texels remain bit-identical across subsequent round trips.

## Native GPU check

Build `tests/d3d9_unorm16_gpu.cpp` against the native D3D9 headers and SDL2.
For the existing ARM64 cross-build dependencies:

```sh
clang++ --target=aarch64-linux-gnu --sysroot=/home/jokubas/work/eoan-sysroot \
  -std=c++17 -O2 -Wall -Wextra -Werror \
  -Iinclude/native/directx -Iinclude/native/windows \
  -I/home/jokubas/work/linux-deps/aarch64/include \
  tests/d3d9_unorm16_gpu.cpp \
  -L/home/jokubas/work/linux-deps/aarch64/lib -lSDL2 -ldl \
  -o /tmp/d3d9-unorm16-gpu.aarch64
```

Copy the executable and library to the handheld, close the game/frontend so
SDL can initialize its KMSDRM backend, then run:

```sh
SDL_VIDEODRIVER=KMSDRM ./d3d9-unorm16-gpu.aarch64 /absolute/path/to/libdxvk_d3d9.so
```

The test creates no visible window. It initializes the native host-memory
callbacks and enables managed staging eviction, reproducing the port's
non-coherent imported-memory path. It exercises 50 actual GPU readback cycles,
alternating full-atlas partial writes with read-only locks.

On the RK3326/Mali-G31 on 2026-11-04:

- Original library: failed on the first readback, expected 509, read 8180.
- Conversion without the post-conversion cache flush: failed on update 4.
- Complete fix: all 50 cycles passed, maximum error 16/65535.
- ARM64 renderer build and CPU test with AddressSanitizer/UBSan passed.
- Portal 2 save `1793793784` (`sp_a2_laser_stairs`): lighting remained normal
  during the user gameplay check and after a save reload. A scanout screenshot
  was captured; the original launch arguments were restored and hash-checked.

The device library was backed up as
`/roms2/ports/portal2/libs/libdxvk_d3d9.so.before-lightmap-readback-20261104`.
The installed fixed library SHA-256 is
`f16fe925dae1e2950993b099ccc6a1bf499403cf8fe5fc12aea79aeda3020d11`.
