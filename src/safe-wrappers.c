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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

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

  @param size A value larger than 0. Otherwise the program will be
  terminated.

  @return A pointer to the allocated memory. Must be freed by the caller
  using free().
*/
void *sMalloc(size_t size)
{
  if(size == 0)
  {
    die("unable to allocate 0 bytes");
  }

  void *data = malloc(size);
  if(data == NULL)
  {
    die("out of memory: failed to allocate %zu bytes", size);
  }

  return data;
}

/** A failsafe wrapper around realloc().

  @param ptr The pointer to the data, which should be reallocated. The data
  may be moved, so the caller shouldn't use this pointer anymore once this
  function returns.
  @param size A value larger than 0. Otherwise the program will be
  terminated.

  @return A pointer to the reallocated memory. Must be freed by the caller
  using free().
*/
void *sRealloc(void *ptr, size_t size)
{
  if(size == 0)
  {
    die("unable to reallocate 0 bytes");
  }

  void *data = realloc(ptr, size);
  if(data == NULL)
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
  caller.
*/
FILE *sFopenRead(const char *path)
{
  FILE *file = fopen(path, "rb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for reading", path);
  }

  return file;
}

/** Safe wrapper around fopen().

  @param path The path to the file, which should be opened for writing.

  @return A file stream that can be used for writing. Must be closed by the
  caller.
*/
FILE *sFopenWrite(const char *path)
{
  FILE *file = fopen(path, "wb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for writing", path);
  }

  return file;
}

/** Safe wrapper around fread(). This function will terminate the program
  on failure. If the given size is larger than the remaining bytes in the
  file stream, it will also terminate the program.

  @param ptr The location for the data that should be read.
  @param size The amount of bytes to read.
  @param stream A file stream.
  @param path The path to the file corresponding to the given stream.
  Needed for printing useful error messages.
*/
void sFread(void *ptr, size_t size, FILE *stream, const char *path)
{
  size_t bytes_read = fread(ptr, 1, size, stream);
  if(bytes_read != size)
  {
    if(feof(stream))
    {
      die("reading \"%s\": reached end of file unexpectedly", path);
    }
    else
    {
      die("IO error while reading \"%s\"", path);
    }
  }
}

/** Safe wrapper around fclose().

  @param stream The stream that should be closed.
  @param path The filepath for which the error message should be shown.
*/
void sFclose(FILE *stream, const char *path)
{
  if(fclose(stream) != 0)
  {
    dieErrno("failed to close \"%s\"", path);
  }
}

/** Safe wrapper around opendir().

  @param path The directories filepath.

  @return A pointer to a valid directory stream.
*/
DIR *sOpenDir(const char *path)
{
  DIR *dir = opendir(path);
  if(dir == NULL)
  {
    dieErrno("failed to open directory \"%s\"", path);
  }

  return dir;
}

/** A safe and simplified wrapper around readdir(). It will skip "." and
  "..".

  @param dir A valid directory stream.
  @param path The directory streams filepath. Needed for printing useful
  error messages.

  @return A pointer to the next entry in the given directory, or NULL if
  the stream reached its end.
*/
struct dirent *sReadDir(DIR *dir, const char *path)
{
  struct dirent *dir_entry;

  do
  {
    int old_errno = errno;
    dir_entry = readdir(dir);

    if(dir_entry == NULL && errno != old_errno)
    {
      dieErrno("failed to read directory \"%s\"", path);
    }
  }
  while(dir_entry != NULL &&
        dir_entry->d_name[0] == '.' &&
        (dir_entry->d_name[1] == '\0' ||
         (dir_entry->d_name[1] == '.' && dir_entry->d_name[2] == '\0')));

  return dir_entry;
}

/** Safe wrapper around closedir().

  @param dir The directory stream that should be closed.
  @param path The directory path of the given stream. Needed for printing
  useful error messages.
*/
void sCloseDir(DIR *dir, const char *path)
{
  if(closedir(dir) != 0)
  {
    dieErrno("failed to close directory \"%s\"", path);
  }
}

/** Returns true if the given filepath exists and terminates the program
  on any unexpected errors.

  @param path The path to the file which existence should be checked.

  @return True if the path exists, false if not.
*/
bool sPathExists(const char *path)
{
  int old_errno = errno;
  if(access(path, F_OK) != 0)
  {
    if(errno != old_errno && errno != ENOENT)
    {
      dieErrno("failed to check existence of \"%s\"", path);
    }

    errno = old_errno;
    return false;
  }

  return true;
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

/** Reads an entire file into memory.

  @param path The path to the file.

  @return A FileContent struct, which content must be freed by the caller
  using free(). The content member will be NULL if the file has a size of
  zero.
*/
FileContent sGetFilesContent(const char *path)
{
  struct stat file_stats = sStat(path);
  if(!S_ISREG(file_stats.st_mode))
  {
    die("\"%s\" is not a regular file", path);
  }

  char *content = NULL;
  if(file_stats.st_size > 0)
  {
    FILE *stream = sFopenRead(path);
    content = sMalloc(file_stats.st_size);
    sFread(content, file_stats.st_size, stream, path);
    sFclose(stream, path);
  }

  return (FileContent){ .content = content, .size = file_stats.st_size };
}
