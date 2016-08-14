def FlagsForFile(filename, **kwargs):
  return {
    'flags': [
      '-std=c99', '-D_XOPEN_SOURCE=600', '-D_FILE_OFFSET_BITS=64',
      '-Wall', '-Wextra', '-Isrc/'
      ],
    'do_cache': True
  }
