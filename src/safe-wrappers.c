/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

/**
  @file safe-wrappers.c Implements various wrapper functions, which handle
  errors by terminating with an error message.
*/

#include "safe-wrappers.h"

#include <stdint.h>
#include <stdlib.h>

#include "error-handling.h"

/** Applies the given stat function on the specified path and terminates
  the program on errors.

  @param path A filepath.
  @param stat_fun A stat function like e.g. stat() or lstat().

  @return Informations about the given file.
*/
static struct stat safeStat(const char *path,
                            int (*stat_fun)(const char *, struct stat *))
{
  struct stat buffer;
  if(stat_fun(path, &buffer) == -1)
  {
    dieErrno("failed to access \"%s\"", path);
  }

  return buffer;
}

/** A failsafe wrapper around malloc().

  @param size A value larger than 0. Standard malloc() handles 0 by
  returning NULL, but this wrapper will terminate the program instead.

  @return A pointer to the allocated memory. Must be freed by the caller
  using free().
*/
void *sMalloc(size_t size)
{
  void *data = malloc(size);
  if(!data)
  {
    die("out of memory: failed to allocate %zu bytes", size);
  }

  return data;
}

/** A failsafe wrapper around realloc().

  @param ptr The pointer to the data, which should be reallocated. The data
  may be moved, so the caller shouldn't use this pointer anymore once this
  function returns.
  @param size A value larger than 0. Standard realloc() handles 0 by
  returning NULL, but this wrapper will terminate the program instead.

  @return A pointer to the reallocated memory. Must be freed by the caller
  using free().
*/
void *sRealloc(void *ptr, size_t size)
{
  void *data = realloc(ptr, size);
  if(!data)
  {
    die("out of memory: failed to reallocate %zu bytes", size);
  }

  return data;
}

/** Adds two sizes and terminates the program on overflows.

  @param a The first summand.
  @param b The second summand.

  @return The sum of a and b.
*/
size_t sSizeAdd(size_t a, size_t b)
{
  if(a > SIZE_MAX - b)
  {
    die("overflow calculating object size");
  }

  return a + b;
}

/** Multiplies two sizes and terminates the program on overflows.

  @param a The first factor.
  @param b The second factor.

  @return The product of a and b.
*/
size_t sSizeMul(size_t a, size_t b)
{
  if(b != 0 && a > SIZE_MAX/b)
  {
    die("overflow calculating object size");
  }

  return a * b;
}

/** Safe wrapper around fopen().

  @param path The path to the file, which should be opened for reading.

  @return A file stream that can be used for reading. Must be closed by the
  caller using fclose().
*/
FILE *sFopenRead(const char *path)
{
  FILE *file = fopen(path, "rb");
  if(!file)
  {
    dieErrno("failed to open \"%s\" for reading", path);
  }

  return file;
}

/** Safe wrapper around fopen().

  @param path The path to the file, which should be opened for writing.

  @return A file stream that can be used for writing. Must be closed by the
  caller using fclose().
*/
FILE *sFopenWrite(const char *path)
{
  FILE *file = fopen(path, "wb");
  if(!file)
  {
    dieErrno("failed to open \"%s\" for writing", path);
  }

  return file;
}

/** A failsafe wrapper around stat().

  @param path A filepath.

  @return Informations about the given file.
*/
struct stat sStat(const char *path)
{
  return safeStat(path, stat);
}

/** A failsafe wrapper around lstat().

  @param path A filepath.

  @return Informations about the given file.
*/
struct stat sLStat(const char *path)
{
  return safeStat(path, lstat);
}
