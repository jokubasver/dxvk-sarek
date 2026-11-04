// Native ARM64/Mali regression. Loads the library supplied as argv[1].
#include <d3d9.h>
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

struct Host {
  uint32_t size;
  void* user;
  void* (*metal_layer)(void*, void*, uint32_t*, uint32_t*);
  void* (*alloc_app)(void*, size_t, size_t);
  void (*free_app)(void*, void*, size_t);
  void (*blocking_begin)(void*);
  void (*blocking_end)(void*);
  uint16_t vendor_id, device_id;
  const char* cache_dir;
  const void* modes;
  uint32_t mode_count;
  int (*transcode_bc)(void*, uint32_t, uint32_t, uint32_t, const void*, void*);
};
static_assert(sizeof(Host) == 96);
static void* alloc(void*, size_t size, size_t alignment) {
  void* p = nullptr;
  return posix_memalign(&p, std::max(alignment, sizeof(void*)), size) ? nullptr : p;
}
static void release(void*, void* p, size_t) { std::free(p); }
static void check(HRESULT hr, const char* call) {
  if (hr < 0) { std::fprintf(stderr, "%s failed: %#x\n", call, unsigned(hr)); std::exit(2); }
}
int main(int argc, char** argv) {
  assert(argc == 2);
  setenv("DXVK_CONFIG", "d3d9.evictManagedOnUnlock=True;d3d9.deferSurfaceCreation=True", 1);
  setenv("DXVK_STATE_CACHE", "0", 1);
  if (SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 2; }
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) { std::fprintf(stderr, "%s\n", dlerror()); return 2; }
  auto init = reinterpret_cast<int (*)(const Host*)>(dlsym(library, "d3d9metal_init"));
  auto create = reinterpret_cast<IDirect3D9* (*)(unsigned)>(dlsym(library, "d3d9metal_Direct3DCreate9"));
  assert(init && create);
  Host host = {}; host.size = sizeof(host); host.alloc_app = alloc; host.free_app = release;
  assert(init(&host) == 0);
  IDirect3D9* d3d = create(D3D_SDK_VERSION);
  assert(d3d);
  D3DPRESENT_PARAMETERS pp = {};
  pp.BackBufferWidth = pp.BackBufferHeight = 32;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.Windowed = TRUE; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9* device = nullptr;
  check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING,
    &pp, &device), "CreateDevice");
  constexpr unsigned width = 8, height = 4;
  std::array<uint16_t, width * height * 4> expected;
  for (unsigned i = 0; i < expected.size(); i++) expected[i] = uint16_t((i * 509) & 65535);
  IDirect3DTexture9* texture = nullptr;
  check(device->CreateTexture(width, height, 1, 0, D3DFMT_A16B16G16R16, D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
  D3DLOCKED_RECT lock;
  check(texture->LockRect(0, &lock, nullptr, 0), "initial LockRect");
  for (unsigned y = 0; y < height; y++)
    std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, expected.data() + y * width * 4, width * 8);
  check(texture->UnlockRect(0), "initial UnlockRect");
  unsigned maximumError = 0;
  for (unsigned update = 0; update < 50; update++) {
    DWORD flags = update & 1 ? D3DLOCK_READONLY : 0;
    check(texture->LockRect(0, &lock, nullptr, flags), "readback LockRect");
    for (unsigned y = 0; y < height; y++) {
      auto* row = reinterpret_cast<uint16_t*>(static_cast<char*>(lock.pBits) + y * lock.Pitch);
      for (unsigned x = 0; x < width * 4; x++) {
        unsigned index = y * width * 4 + x;
        unsigned error = unsigned(std::abs(int(row[x]) - int(expected[index])));
        maximumError = std::max(maximumError, error);
        if (error > 16) {
          std::fprintf(stderr, "FAIL update=%u texel=%u component=%u expected=%u read=%u error=%u\n",
            update, index / 4, index % 4, expected[index], row[x], error);
          return 1;
        }
        // Track the quantized value actually retained by the emulation.
        expected[index] = row[x];
      }
    }
    if (!(flags & D3DLOCK_READONLY)) {
      // Match Source: lock the whole atlas, write just one surface.
      unsigned pixel = update % (width * height);
      auto* row = reinterpret_cast<uint16_t*>(static_cast<char*>(lock.pBits) + (pixel / width) * lock.Pitch);
      for (unsigned component = 0; component < 4; component++)
        row[(pixel % width) * 4 + component] = expected[pixel * 4 + component] = uint16_t(update * 997 + component);
    }
    check(texture->UnlockRect(0), "readback UnlockRect");
  }
  texture->Release(); device->Release(); d3d->Release(); SDL_Quit();
  std::printf("PASS: 50 GPU readback cycles, full-atlas partial writes and read-only locks; max error %u/65535\n", maximumError);
}
