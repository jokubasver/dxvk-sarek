#pragma once

#include <cstddef>
#include <cstdint>

namespace dxvk {

  /**
   * \brief Memory provided by the host application
   *
   * Set when DXVK runs inside a process that can only dereference part
   * of its own address space, e.g. a 32-bit guest translated to arm64.
   * Every mapped allocation is then taken from this memory and imported
   * with VK_EXT_external_memory_host, so that pointers returned by Lock
   * are usable by the application.
   */
  struct HostMemoryCallbacks {
    void* user = nullptr;
    void* (*alloc)(void* user, size_t size, size_t align) = nullptr;
    void  (*free) (void* user, void* ptr, size_t size) = nullptr;
  };

  void setHostMemoryCallbacks(const HostMemoryCallbacks& callbacks);

  /**
   * \brief Host memory callbacks
   * \returns Callbacks, or \c nullptr if the host did not set any
   */
  const HostMemoryCallbacks* getHostMemoryCallbacks();

  /**
   * \brief Makes CPU writes to host memory visible to the GPU
   *
   * Host memory is imported as cached, non-coherent memory on GPUs
   * without I/O coherency. Cleans the CPU caches over the range when
   * host memory is in use, does nothing otherwise.
   * \param [in] ptr Start of the range
   * \param [in] size Range size in bytes
   */
  void flushHostMemory(const void* ptr, size_t size);

  /**
   * \brief Makes GPU writes to host memory visible to the CPU
   *
   * Cleans and invalidates the CPU caches over the range.
   * \param [in] ptr Start of the range
   * \param [in] size Range size in bytes
   */
  void invalidateHostMemory(const void* ptr, size_t size);

  /**
   * \brief BC to ASTC transcoder provided by the host application
   *
   * Lets GPUs without BC textures keep DXT textures compressed:
   * one level of DXT data (format is the D3DFORMAT FOURCC) becomes
   * packed ASTC 4x4 blocks. Returns nonzero on success.
   */
  struct HostBcTranscoder {
    void* user = nullptr;
    int (*transcode)(void* user, uint32_t format, uint32_t width, uint32_t height, const void* bc, void* astc) = nullptr;
  };

  void setHostBcTranscoder(const HostBcTranscoder& transcoder);

  /**
   * \brief Host BC transcoder
   * \returns Transcoder, or \c nullptr if the host did not set one
   */
  const HostBcTranscoder* getHostBcTranscoder();

}
