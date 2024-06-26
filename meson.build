project('carefulsource', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++17'],
  meson_version: '>=0.51.0',
  version: '1'
)

cxx = meson.get_compiler('cpp')

gcc_syntax = cxx.get_argument_syntax() == 'gcc'

if get_option('buildtype') == 'release'
  add_project_arguments(gcc_syntax ? ['-fno-math-errno', '-fno-trapping-math'] : '/GS-', language: 'cpp')
endif

if gcc_syntax
  vapoursynth_dep = dependency('vapoursynth', version: '>=55').partial_dependency(compile_args: true, includes: true)
  install_dir = vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth'
else
  vapoursynth_dep = []
  install_dir = get_option('libdir') / 'vapoursynth'
endif

lcms2_dep = dependency('lcms2')
libpng_dep = dependency('libpng')
libjpeg_dep = dependency('libjpeg')

sources = [
  'carefulsource.cpp',
  'carefulsource.h',
  'decoder_base.h',
  'decoder_png.cpp',
  'decoder_png.h',
  'decoder_jpeg.cpp',
  'decoder_jpeg.h',
  'cmyk.h',
]

libs = []

shared_module('carefulsource', sources,
  dependencies: [vapoursynth_dep, lcms2_dep, libpng_dep, libjpeg_dep],
  link_with: libs,
  install: true,
  install_dir: install_dir,
  gnu_symbol_visibility: 'hidden'
)
