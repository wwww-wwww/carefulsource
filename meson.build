project('carefulsource', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
  meson_version: '>=0.51.0',
  version: '1'
)

cxx = meson.get_compiler('cpp')

gcc_syntax = cxx.get_argument_syntax() == 'gcc'

if get_option('buildtype') == 'release'
  add_project_arguments(gcc_syntax ? ['-fno-math-errno', '-fno-trapping-math'] : ['/GS-'], language: 'cpp')
endif

if gcc_syntax
  vapoursynth_dep = dependency('vapoursynth', version: '>=55').partial_dependency(compile_args: true, includes: true)
  install_dir = vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth'
else
  vapoursynth_inc = include_directories('C:/Program Files/VapourSynth/sdk/include/vapoursynth', is_system: true)
  vapoursynth_dep = declare_dependency(include_directories: vapoursynth_inc)
  install_dir = get_option('libdir') / 'vapoursynth'
endif

lcms2_dep = dependency('lcms2')
libpng_dep = dependency('libpng')
libjpeg_dep = dependency('libjpeg')

sources = [
  'src/carefulsource.cpp',
  'src/decoder_png.cpp',
  'src/decoder_jpeg.cpp',
]

deps = [vapoursynth_dep, lcms2_dep, libpng_dep, libjpeg_dep]
libs = []

if get_option('py_ext')
  python = import('python')
  py = python.find_installation(pure: false)
  py.extension_module('vscarefulsource_ext', sources,
    dependencies: deps,
    link_with: libs,
    install: true,
  )
else
  shared_module('carefulsource', sources,
    dependencies: deps,
    link_with: libs,
    install: true,
    install_dir: install_dir,
    gnu_symbol_visibility: 'hidden',
  )
endif
