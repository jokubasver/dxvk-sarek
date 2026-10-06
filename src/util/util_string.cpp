#include "util_string.h"

namespace dxvk::str {

#ifdef _WIN32
  std::string fromws(const WCHAR *ws) {
    size_t len = ::WideCharToMultiByte(CP_UTF8,
      0, ws, -1, nullptr, 0, nullptr, nullptr);

    if (len <= 1)
      return "";

    len -= 1;

    std::string result;
    result.resize(len);
    ::WideCharToMultiByte(CP_UTF8, 0, ws, -1,
      &result.at(0), len, nullptr, nullptr);
    return result;
  }


  void tows(const char* mbs, WCHAR* wcs, size_t wcsLen) {
    ::MultiByteToWideChar(
      CP_UTF8, 0, mbs, -1,
      wcs, wcsLen);
  }

  std::wstring tows(const char* mbs) {
    size_t len = ::MultiByteToWideChar(CP_UTF8,
      0, mbs, -1, nullptr, 0);
    
    if (len <= 1)
      return L"";

    len -= 1;

    std::wstring result;
    result.resize(len);
    ::MultiByteToWideChar(CP_UTF8, 0, mbs, -1,
      &result.at(0), len);
    return result;
  }

#else

  // Native builds: wchar_t holds UTF-32, strings are UTF-8.
  std::string fromws(const WCHAR* ws) {
    std::string result;

    for (; *ws; ws++) {
      uint32_t ch = uint32_t(*ws);

      if (ch < 0x80) {
        result += char(ch);
      } else if (ch < 0x800) {
        result += char(0xC0 | (ch >> 6));
        result += char(0x80 | (ch & 0x3F));
      } else if (ch < 0x10000) {
        result += char(0xE0 | (ch >> 12));
        result += char(0x80 | ((ch >> 6) & 0x3F));
        result += char(0x80 | (ch & 0x3F));
      } else if (ch < 0x110000) {
        result += char(0xF0 | (ch >> 18));
        result += char(0x80 | ((ch >> 12) & 0x3F));
        result += char(0x80 | ((ch >> 6) & 0x3F));
        result += char(0x80 | (ch & 0x3F));
      } else {
        result += '?';
      }
    }

    return result;
  }


  std::wstring tows(const char* mbs) {
    std::wstring result;
    auto s = reinterpret_cast<const uint8_t*>(mbs);

    while (*s) {
      uint32_t first = *s;
      uint32_t ch    = uint32_t('?');
      size_t   len   = 1;

      if (first < 0x80) {
        ch = first;
      } else if (first >= 0xC0 && first < 0xE0 && s[1]) {
        ch = ((first & 0x1F) << 6) | (s[1] & 0x3F);
        len = 2;
      } else if (first >= 0xE0 && first < 0xF0 && s[1] && s[2]) {
        ch = ((first & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        len = 3;
      } else if (first >= 0xF0 && first < 0xF8 && s[1] && s[2] && s[3]) {
        ch = ((first & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        len = 4;
      }

      result += wchar_t(ch);
      s += len;
    }

    return result;
  }


  void tows(const char* mbs, WCHAR* wcs, size_t wcsLen) {
    if (!wcsLen)
      return;

    std::wstring result = tows(mbs);
    size_t len = std::min(result.size(), wcsLen - 1);

    std::memcpy(wcs, result.data(), len * sizeof(WCHAR));
    wcs[len] = 0;
  }

#endif

}
