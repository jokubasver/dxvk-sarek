wsi_src = [
  'wsi_platform.cpp',
]

wsi_deps = []

if platform == 'windows'
  wsi_src += [
    'win32/wsi_monitor_win32.cpp',
    'win32/wsi_platform_win32.cpp',
    'win32/wsi_window_win32.cpp',
  ]
else
  # Only the headers: the driver loads SDL2 at run time.
  wsi_src += [
    'sdl2/wsi_monitor_sdl2.cpp',
    'sdl2/wsi_platform_sdl2.cpp',
    'sdl2/wsi_window_sdl2.cpp',
  ]
  wsi_deps += [ lib_sdl2.partial_dependency(compile_args: true, includes: true) ]
endif

wsi_lib = static_library('wsi', wsi_src,
  dependencies        : wsi_deps,
  include_directories : [ dxvk_include_path ])

wsi_dep = declare_dependency(
  link_with           : [ wsi_lib ])
