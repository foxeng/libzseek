project('libzseek', 'c',
    default_options: ['c_std=c11', 'warning_level=3', 'werror=true'],
    license: 'BSD-3-Clause',
    version: '3.1.0')
add_project_arguments('-D_GNU_SOURCE', language: 'c')


threads_dep = dependency('threads')
zstd_dep = dependency('libzstd', version: '>=1.4.9')
lz4_dep = dependency('liblz4', version: '>=1.8.3')
check_dep = dependency('check')
# From https://mesonbuild.com/howtox.html#add-math-library-lm-portably
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

libzseek = library('zseek',
    'src/buffer.c',
    'src/cache.c',
    'src/common.c',
    'src/compress.c',
    'src/decompress.c',
    'src/seek_table.c',
    dependencies: [threads_dep, zstd_dep, lz4_dep],
    install: true,
    soversion: '3.1.0')
libzseek_dep = declare_dependency(
    include_directories: 'src',
    link_with: libzseek)


libzseek_example = executable('libzseek_example',
    'test/example.c',
    dependencies: libzseek_dep)
libzseek_benchmark = executable('libzseek_benchmark',
    'test/benchmark.c',
    dependencies: [libzseek_dep, m_dep])

test_cache = executable('test_cache',
    'test/test_cache.c',
    dependencies: [libzseek_dep, check_dep])
test('test_cache', test_cache)

test_buffer = executable('test_buffer',
    'test/test_buffer.c',
    dependencies: [libzseek_dep, check_dep])
test('test_buffer', test_buffer)


install_headers('src/zseek.h')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libzseek,
    description: 'Random access decompression library',
    filebase: 'libzseek',
    name: meson.project_name(),
    url: 'https://github.com/foxeng/libzseek')
