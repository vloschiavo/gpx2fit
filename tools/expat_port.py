# Minimal Emscripten port for builds whose Emscripten lacks an expat port.
# The archive and hash are pinned so `make wasm` remains reproducible.

import os

VERSION = '2.7.4'
HASH = '3fa9d9092f85f585351ee3f9e46009e289faa1288401e59e93513e2661f70742dfc3daee8639d2db2d8dc8348c01846ad5040ad8baf56f964778b075c3296bdf'


def get(ports, settings, shared):
  ports.fetch_project(
      'expat_port',
      f'https://github.com/libexpat/libexpat/releases/download/R_{VERSION.replace(".", "_")}/expat-{VERSION}.tar.gz',
      sha512hash=HASH,
  )

  def create(final):
    source_path = ports.get_dir('expat_port', f'expat-{VERSION}')
    lib_dir = os.path.join(source_path, 'lib')
    config = '''#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H 1
#define BYTEORDER 1234
#define STDC_HEADERS 1
#define XML_CONTEXT_BYTES 1024
#define XML_DEV_URANDOM 1
#define XML_DTD 1
#define XML_GE 1
#define XML_NS 1
#endif
'''
    with open(os.path.join(lib_dir, 'expat_config.h'), 'w') as f:
      f.write(config)

    ports.install_headers(lib_dir, 'expat*.h')
    ports.build_port(
        source_path,
        final,
        'expat',
        includes=[lib_dir],
        srcs=['lib/xmlparse.c', 'lib/xmlrole.c', 'lib/xmltok.c'],
    )

  return [shared.cache.get_lib('libexpat.a', create, what='port')]


def clear(ports, settings, shared):
  shared.cache.erase_lib('libexpat.a')


def show():
  return 'expat (local fallback port; MIT license)'
