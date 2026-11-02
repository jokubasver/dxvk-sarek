#pragma once

#include <string>
#include <vector>

#include "dxvk_include.h"

#include "../util/thread.h"

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief Pipeline cache
   *
   * Where the driver keeps no disk cache of its own (Arm's Mali driver
   * on Linux), every pipeline would be compiled again in every run: the
   * state cache only knows which pipelines to build, not the binaries.
   * There, see DxvkOptions::pipelineCache, this hands the driver a
   * VkPipelineCache and keeps its data on disk beside the state cache.
   * Elsewhere the handle stays null, as before.
   */
  class DxvkPipelineCache : public RcObject {

  public:

    DxvkPipelineCache(
      const DxvkDevice*         device,
            bool                persistent);

    ~DxvkPipelineCache();

    /**
     * \brief Pipeline cache handle
     * \returns Pipeline cache handle, may be null
     */
    VkPipelineCache handle() const {
      return m_handle;
    }

    /**
     * \brief Writes the cache to disk in the background
     *
     * The driver hands out the cache only as one opaque blob, so every
     * save rewrites the whole file (tens of MB). Saving on a timer did
     * that in the middle of play; callers ask for it at level loads,
     * and the cache is saved once more on shutdown.
     */
    void requestSave();

  private:

    Rc<vk::DeviceFn>            m_vkd;
    VkPhysicalDeviceProperties  m_properties;
    VkPipelineCache             m_handle = VK_NULL_HANDLE;

    std::string                 m_fileName;
    size_t                      m_savedSize = 0;

    dxvk::mutex                 m_mutex;
    dxvk::condition_variable    m_cond;
    bool                        m_stopped = false;
    bool                        m_saveRequested = false;
    dxvk::thread                m_thread;

    std::vector<char> readFile() const;

    bool isCompatible(
      const std::vector<char>&  data) const;

    size_t currentSize() const;

    void save();

    void runSaver();

  };

}
