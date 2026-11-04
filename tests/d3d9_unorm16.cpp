// Standalone CPU regression: c++ -std=c++17 -O2 -Wall -Wextra -Werror
// tests/d3d9_unorm16.cpp -o /tmp/d3d9-unorm16 && /tmp/d3d9-unorm16
#include "../src/d3d9/d3d9_unorm16.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static double decodeHalf(uint16_t bits) {
  unsigned exponent = (bits >> 10) & 31;
  unsigned mantissa = bits & 1023;
  double value = exponent == 0
    ? std::ldexp(double(mantissa), -24)
    : exponent == 31
      ? (mantissa ? NAN : INFINITY)
      : std::ldexp(double(1024 + mantissa), int(exponent) - 25);
  return bits & 0x8000 ? -value : value;
}

static uint16_t encodeUnorm(uint16_t value) {
  _Float16 half = _Float16(float(value) / 65535.0f);
  uint16_t bits;
  std::memcpy(&bits, &half, sizeof(bits));
  return bits;
}

int main() {
  for (unsigned bits = 0; bits < 65536; bits++) {
    double value = decodeHalf(uint16_t(bits));
    unsigned expected = std::isnan(value) || value <= 0.0 ? 0
      : value >= 1.0 ? 65535 : unsigned(std::lround(value * 65535.0));
    assert(dxvk::convertFloat16ToUnorm16(uint16_t(bits)) == expected);
  }

  unsigned maxError = 0;
  for (unsigned value = 0; value < 65536; value++) {
    uint16_t half = encodeUnorm(uint16_t(value));
    uint16_t restored = dxvk::convertFloat16ToUnorm16(half);
    maxError = std::max(maxError, unsigned(std::abs(int(restored) - int(value))));
    assert(encodeUnorm(restored) == half);
  }
  assert(maxError <= 16);

  // Full-page locks may rewrite only a small surface. Preserve every other
  // GPU texel across repeated readback / partial update / upload cycles.
  std::array<uint16_t, 256> image;
  for (unsigned i = 0; i < image.size(); i++)
    image[i] = encodeUnorm(uint16_t(i * 257));
  for (unsigned update = 0; update < 100; update++) {
    auto before = image;
    auto mapped = image;
    for (auto& value : mapped)
      value = dxvk::convertFloat16ToUnorm16(value);
    unsigned pixel = update % 64;
    for (unsigned component = 0; component < 4; component++)
      mapped[pixel * 4 + component] = uint16_t(update * 313 + component);
    for (unsigned i = 0; i < image.size(); i++) {
      image[i] = encodeUnorm(mapped[i]);
      if (i / 4 != pixel)
        assert(image[i] == before[i]);
    }
  }
  std::printf("PASS: 65536 half patterns, 65536 UNORM round trips (max error %u/65535), 100 partial atlas updates\n", maxError);
}
