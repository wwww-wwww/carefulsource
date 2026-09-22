project('carefulsource', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20', 'default_library=static', 'wrap_mode=forcefallback'],
  meson_version: '>=1.1.0',
  version: '3'
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
vapoursynth_dep = declare_dependency(compile_args: '-I' + vapoursynth_include_command.stdout().strip())

imagedecoder_dep = dependency('imagedecoder', static: true)

sources = [
  'src/carefulsource.cpp',
]

deps = [vapoursynth_dep, imagedecoder_dep]
libs = []

shared_module('carefulsource', sources,
  dependencies: deps,
  link_with: libs,
  install: true,
  install_dir: py.get_install_dir(pure: false) / 'vapoursynth' / 'plugins',
  gnu_symbol_visibility: 'hidden',
)
