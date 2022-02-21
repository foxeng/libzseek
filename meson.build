project('libzseek', 'c',
    default_options: ['c_std=c11', 'warning_level=3', 'werror=true'],
    license: 'BSD-3-Clause',
    meson_version: '>=0.50.0',
    version: '3.0.2')
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
    gnu_symbol_visibility: 'hidden',
    install: true,
    version: '3.0.2')
libzseek_dep = declare_dependency(
    include_directories: 'src',
    link_with: libzseek)


libzseek_example = executable('libzseek_example',
    'test/example.c',
    dependencies: libzseek_dep)
libzseek_benchmark = executable('libzseek_benchmark',
    'test/benchmark.c',
    dependencies: [libzseek_dep, m_dep])

# Extract object file to use directly un-exported symbols. See
# https://mesonbuild.com/Build-targets.html#object-files
cache_o = libzseek.extract_objects('src/cache.c')
test_cache = executable('test_cache',
    'test/test_cache.c',
    dependencies: [check_dep],
    objects: [cache_o])
test('test_cache', test_cache)

buffer_o = libzseek.extract_objects('src/buffer.c')
test_buffer = executable('test_buffer',
    'test/test_buffer.c',
    dependencies: [check_dep],
    objects: [buffer_o])
test('test_buffer', test_buffer)


install_headers('src/zseek.h')

project_desc = 'Random access decompression library'
pkg_mod = import('pkgconfig')
pkg_mod.generate(libzseek,
    description: project_desc,
    filebase: 'libzseek',
    name: meson.project_name(),
    url: 'https://github.com/foxeng/libzseek')


doxyfile = configure_file(input: 'Doxyfile.in',
    output: 'Doxyfile',
    configuration: {
        'NAME': meson.project_name(),
        'DESCRIPTION': project_desc,
        'VERSION': meson.project_version(),
        'TOP_SRCDIR': meson.current_source_dir(),
        'TOP_BUILDDIR': meson.current_build_dir(),
    })
doxygen = find_program('doxygen', disabler: true, required: false)
doc_target = custom_target('doc',
    input: doxyfile,
    output: 'doc',
    command: [doxygen, doxyfile])
