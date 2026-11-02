#include <cstdio>
#include <cstring>
#include <fstream>

#include "dxvk_device.h"
#include "dxvk_pipecache.h"

#include "../util/util_stall.h"

namespace dxvk {

  namespace {

    // The folder of the state cache (DxvkStateCache::getCacheDir)
    std::string getCacheDir() {
      std::string cachePath = env::getEnvVar("DXVK_STATE_CACHE_PATH");

      if (cachePath.empty()) {
#ifdef _WIN32
        cachePath = env::getEnvVar("LOCALAPPDATA");
#endif

        if (cachePath.empty())
          cachePath = env::getEnvVar("XDG_CACHE_HOME");

        if (cachePath.empty()) {
          cachePath = env::getEnvVar("HOME");

          if (!cachePath.empty())
            cachePath += "/.cache";
        }

        if (!cachePath.empty())
          cachePath += "/dxvk";
      }

      return cachePath;
    }

  }


  DxvkPipelineCache::DxvkPipelineCache(
    const DxvkDevice*         device,
          bool                persistent)
  : m_vkd       (device->vkd()),
    m_properties(device->adapter()->deviceProperties()) {
    bool enable = device->adapter()->matchesDriver(VK_DRIVER_ID_ARM_PROPRIETARY);
    applyTristate(enable, device->config().pipelineCache);

    if (!enable)
      return;

    std::vector<char> data;

    if (persistent) {
      std::string dir = getCacheDir();

      if (!dir.empty()) {
        if (*dir.rbegin() != '/')
          dir += '/';

        m_fileName = dir + env::getExeBaseName() + ".dxvk-pipelines";
        data = readFile();

        if (!data.empty() && !isCompatible(data)) {
          Logger::warn("DXVK: Pipeline cache file is from another driver or GPU, starting it over");
          data.clear();
        }
      }
    }

    VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    info.initialDataSize = data.size();
    info.pInitialData    = data.empty() ? nullptr : data.data();

    if (m_vkd->vkCreatePipelineCache(m_vkd->device(), &info, nullptr, &m_handle) != VK_SUCCESS) {
      info.initialDataSize = 0;
      info.pInitialData    = nullptr;

      if (m_vkd->vkCreatePipelineCache(m_vkd->device(), &info, nullptr, &m_handle) != VK_SUCCESS) {
        Logger::warn("DXVK: Failed to create pipeline cache");
        m_handle = VK_NULL_HANDLE;
        return;
      }
    }

    m_savedSize = currentSize();

    Logger::info(str::format("DXVK: Pipeline cache: ", data.size() >> 10, " kB read",
      m_fileName.empty() ? ", not kept on disk" : str::format(" from ", m_fileName)));

    if (!m_fileName.empty())
      m_thread = dxvk::thread([this] () { runSaver(); });
  }


  DxvkPipelineCache::~DxvkPipelineCache() {
    if (m_thread.joinable()) {
      { std::lock_guard<dxvk::mutex> lock(m_mutex);
        m_stopped = true;
      }

      m_cond.notify_one();
      m_thread.join();

      save();
    }

    if (m_handle != VK_NULL_HANDLE)
      m_vkd->vkDestroyPipelineCache(m_vkd->device(), m_handle, nullptr);
  }


  std::vector<char> DxvkPipelineCache::readFile() const {
    std::ifstream file(str::topath(m_fileName.c_str()).c_str(), std::ios_base::binary | std::ios_base::ate);

    if (!file)
      return { };

    std::streamoff size = file.tellg();

    if (size <= 0 || size > (std::streamoff(256) << 20))
      return { };

    std::vector<char> data(static_cast<size_t>(size));
    file.seekg(0);

    if (!file.read(data.data(), size))
      return { };

    return data;
  }


  bool DxvkPipelineCache::isCompatible(
    const std::vector<char>&  data) const {
    // VkPipelineCacheHeaderVersionOne: size, version, vendor, device, UUID
    if (data.size() < 16 + VK_UUID_SIZE)
      return false;

    uint32_t header[4];
    std::memcpy(header, data.data(), sizeof(header));

    return header[0] >= 16 + VK_UUID_SIZE
        && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
        && header[2] == m_properties.vendorID
        && header[3] == m_properties.deviceID
        && !std::memcmp(data.data() + 16, m_properties.pipelineCacheUUID, VK_UUID_SIZE);
  }


  size_t DxvkPipelineCache::currentSize() const {
    size_t size = 0;

    if (m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_handle, &size, nullptr) != VK_SUCCESS)
      return 0;

    return size;
  }


  void DxvkPipelineCache::save() {
    size_t size = currentSize();

    // The cache only grows, so an unchanged size means nothing new
    if (!size || size == m_savedSize)
      return;

    double t0 = stall::now();

    std::vector<char> data(size);

    // VK_INCOMPLETE: it grew meanwhile, the next round takes it
    if (m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_handle, &size, data.data()) != VK_SUCCESS)
      return;

    data.resize(size);

    // Written beside the old file and renamed over it, so that a run
    // cut short never leaves half a file
    std::string tempName = m_fileName + ".tmp";

    { std::ofstream file(str::topath(tempName.c_str()).c_str(), std::ios_base::binary | std::ios_base::trunc);

      if (!file && env::createDirectory(getCacheDir()))
        file = std::ofstream(str::topath(tempName.c_str()).c_str(), std::ios_base::binary | std::ios_base::trunc);

      if (!file || !file.write(data.data(), data.size()))
        return;
    }

    if (std::rename(tempName.c_str(), m_fileName.c_str()) == 0)
      m_savedSize = size;

    double t1 = stall::now();

    Logger::info(str::format("DXVK: Pipeline cache: saved ", size >> 10, " kB in ",
      uint32_t(t1 - t0), " ms (at ", uint32_t(t0), " ms)"));
  }


  void DxvkPipelineCache::requestSave() {
    if (!m_thread.joinable())
      return;

    { std::lock_guard<dxvk::mutex> lock(m_mutex);
      m_saveRequested = true;
    }

    m_cond.notify_one();
  }


  void DxvkPipelineCache::runSaver() {
    env::setThreadName("dxvk-pipe-cache");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (!m_stopped) {
      m_cond.wait(lock, [this] {
        return m_stopped || m_saveRequested;
      });

      if (m_stopped)
        break;

      m_saveRequested = false;

      lock.unlock();
      save();
      lock.lock();
    }
  }

}
