project('carefulsource', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
  meson_version: '>=1.1.0',
  version: '1'
)

cxx = meson.get_compiler('cpp')
gcc_syntax = cxx.get_argument_syntax() == 'gcc'

if get_option('buildtype') == 'release'
  add_project_arguments(gcc_syntax ? ['-fno-math-errno', '-fno-trapping-math'] : ['/GS-'], language: 'cpp')
endif

py = import('python').find_installation(pure: false)

vapoursynth_include_command = run_command(
  py,
  '-c',
  'import vapoursynth as vs; print(vs.get_include())',
  check: true,
)
vapoursynth_inc = include_directories(vapoursynth_include_command.stdout().strip(), is_system: true)
vapoursynth_dep = declare_dependency(include_directories: vapoursynth_inc)

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
  py.extension_module('vscarefulsource_ext', sources,
    dependencies: deps,
    link_with: libs,
    install: true,
  )
  py.install_sources(['vscarefulsource/__init__.py'], subdir: 'vscarefulsource')
else
  shared_module('carefulsource', sources,
    dependencies: deps,
    link_with: libs,
    install: true,
    install_dir: py.get_install_dir(pure: false) / 'vapoursynth' / 'plugins',
    gnu_symbol_visibility: 'hidden',
  )
endif