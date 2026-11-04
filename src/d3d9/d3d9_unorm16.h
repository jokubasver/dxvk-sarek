#pragma once

#include <cstdint>

namespace dxvk {

  // Restore the application's UNORM16 representation from the fp16 backing
  // used when A16B16G16R16 filtering is unavailable. Negative values and NaNs
  // become zero; values >= 1 saturate. Round only after the exact integer scale.
  inline uint16_t convertFloat16ToUnorm16(uint16_t value) {
    if ((value & 0x8000u) || value > 0x7c00u)
      return 0;

    if (value >= 0x3c00u)
      return 65535;

    uint32_t exponent = value >> 10;
    uint32_t mantissa = value & 0x3ffu;
    uint32_t shift = 24;

    if (exponent) {
      mantissa |= 0x400u;
      shift = 25 - exponent;
    }

    return uint16_t((mantissa * 65535u + (1u << (shift - 1))) >> shift);
  }

}
