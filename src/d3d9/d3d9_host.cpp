// d3d9metal host interface (portal2-recomp's runtime/d3d9/d3d9metal.h):
// lets a translated 32-bit application load DXVK as its native Direct3D 9.
// HWND values are SDL_Window pointers (SDL2 WSI), so the metal_layer
// callback is not needed. Memory that Lock returns must come from the
// host's alloc_app, which the memory allocator imports for every mapped
// allocation.

#include "../util/util_env.h"
#include "../util/util_host_memory.h"
#include "../util/log/log.h"

#include "d3d9_include.h"

#include <cstdint>
#include <cstdlib>

namespace dxvk {

  struct D3D9HostDisplayMode {
    uint32_t width, height, refresh_hz;
  };

  struct D3D9Host {
    uint32_t size;
    void* user;
    void* (*metal_layer)(void* user, void* hwnd, uint32_t* width, uint32_t* height);
    void* (*alloc_app)(void* user, size_t size, size_t align);
    void  (*free_app)(void* user, void* ptr, size_t size);
    void  (*blocking_begin)(void* user);
    void  (*blocking_end)(void* user);
    uint16_t vendor_id, device_id;
    const char* cache_dir;
    const D3D9HostDisplayMode* modes;
    uint32_t mode_count;
  };

  static_assert(offsetof(D3D9Host, alloc_app) == 24 && offsetof(D3D9Host, vendor_id) == 56
             && offsetof(D3D9Host, cache_dir) == 64 && sizeof(D3D9Host) == 88, "d3d9metal.h layout");

  constexpr int D3D9HostOk       =  0;
  constexpr int D3D9HostNull     = -1;
  constexpr int D3D9HostInvalid  = -2;
  constexpr int D3D9HostAlready  = -3;

  static bool g_hostInitialized = false;

  HRESULT CreateD3D9(
          bool           Extended,
          IDirect3D9Ex** ppDirect3D9Ex);

}


extern "C" {

  DLLEXPORT int d3d9metal_init(const dxvk::D3D9Host* host) {
    using namespace dxvk;

    if (!host)
      return D3D9HostNull;

    if (host->size < offsetof(D3D9Host, vendor_id) || !host->alloc_app || !host->free_app)
      return D3D9HostInvalid;

    if (g_hostInitialized)
      return D3D9HostAlready;

    HostMemoryCallbacks callbacks;
    callbacks.user  = host->user;
    callbacks.alloc = host->alloc_app;
    callbacks.free  = host->free_app;
    setHostMemoryCallbacks(callbacks);

    // The pipeline cache goes where the host keeps its caches,
    // unless the user picked a place
    if (host->size >= offsetof(D3D9Host, modes) && host->cache_dir
     && env::getEnvVar("DXVK_STATE_CACHE_PATH").empty())
      setenv("DXVK_STATE_CACHE_PATH", host->cache_dir, 1);

    g_hostInitialized = true;
    Logger::info("D3D9: initialized by the host, mapped memory comes from the application");
    return D3D9HostOk;
  }


  DLLEXPORT void* d3d9metal_Direct3DCreate9(unsigned nSDKVersion) {
    IDirect3D9Ex* pDirect3D = nullptr;
    dxvk::CreateD3D9(false, &pDirect3D);
    return pDirect3D;
  }

}
