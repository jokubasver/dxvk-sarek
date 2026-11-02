#pragma once

#include <chrono>

#include "util_env.h"
#include "util_time.h"

namespace dxvk::stall {

  /**
   * \brief Whether stalls are logged
   *
   * DXVK_STALL_LOG=1 logs work that can hold up a frame (pipelines and
   * shader variants compiled on the render path, pipeline cache saves)
   * and frames that took long, each with a time stamp, so hitches can
   * be matched to their cause.
   */
  inline bool enabled() {
    static const bool s_enabled = env::getEnvVar("DXVK_STALL_LOG") == "1";
    return s_enabled;
  }

  inline const high_resolution_clock::time_point g_start = high_resolution_clock::now();

  /**
   * \brief Milliseconds since the library was loaded
   */
  inline double now() {
    return std::chrono::duration<double, std::milli>(
      high_resolution_clock::now() - g_start).count();
  }

}
