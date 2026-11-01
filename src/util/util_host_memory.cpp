#include "util_host_memory.h"

#include <cstdint>

namespace dxvk {

  static HostMemoryCallbacks g_hostMemory;

  void setHostMemoryCallbacks(const HostMemoryCallbacks& callbacks) {
    g_hostMemory = callbacks;
  }


  const HostMemoryCallbacks* getHostMemoryCallbacks() {
    return g_hostMemory.alloc && g_hostMemory.free ? &g_hostMemory : nullptr;
  }


#if defined(__aarch64__)
  // Linux lets user space maintain the data cache by address (SCTLR_EL1.UCI),
  // which is what the driver's own flush for cached memory amounts to,
  // without a system call per range.
  template<bool Invalidate>
  static void maintainCache(const void* ptr, size_t size) {
    if (!g_hostMemory.alloc || !size)
      return;

    uint64_t ctr;
    asm volatile("mrs %0, ctr_el0" : "=r" (ctr));
    const uintptr_t line = uintptr_t(4) << ((ctr >> 16) & 0xf);

    uintptr_t address = uintptr_t(ptr) & ~(line - 1);
    const uintptr_t end = uintptr_t(ptr) + size;

    asm volatile("dsb ish" ::: "memory");

    for (; address < end; address += line) {
      if (Invalidate)
        asm volatile("dc civac, %0" :: "r" (address) : "memory");
      else
        asm volatile("dc cvac, %0" :: "r" (address) : "memory");
    }

    asm volatile("dsb sy" ::: "memory");
  }
#else
  template<bool Invalidate>
  static void maintainCache(const void*, size_t) { }
#endif


  void flushHostMemory(const void* ptr, size_t size) {
    maintainCache<false>(ptr, size);
  }


  void invalidateHostMemory(const void* ptr, size_t size) {
    maintainCache<true>(ptr, size);
  }

}
