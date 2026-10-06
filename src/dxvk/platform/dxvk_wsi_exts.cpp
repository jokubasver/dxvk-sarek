#include "../dxvk_platform_exts.h"

#include "../../wsi/wsi_platform.h"

namespace dxvk {

  DxvkPlatformExts DxvkPlatformExts::s_instance;

  std::string_view DxvkPlatformExts::getName() {
    return "WSI driver";
  }


  DxvkNameSet DxvkPlatformExts::getInstanceExtensions() {
    // The surface extensions the window system needs (for SDL2 on KMSDRM,
    // VK_KHR_display), as the WSI driver reports them.
    DxvkNameSet names;

    for (const char* name : wsi::getInstanceExtensions())
      names.add(name);

    return names;
  }


  DxvkNameSet DxvkPlatformExts::getDeviceExtensions(
          uint32_t      adapterId) {
    return DxvkNameSet();
  }


  void DxvkPlatformExts::initInstanceExtensions() {

  }


  void DxvkPlatformExts::initDeviceExtensions(
    const DxvkInstance* instance) {

  }

}
