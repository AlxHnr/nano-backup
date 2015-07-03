def FlagsForFile(filename, **kwargs):
  return {
    'flags': [
      '-std=c99', '-D_POSIX_C_SOURCE=200112L', '-Wall', '-Wextra', '-Isrc/'
      ],
    'do_cache': True
  }
