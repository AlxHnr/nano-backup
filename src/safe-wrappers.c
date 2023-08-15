#include "safe-wrappers.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#include "CRegion/global-region.h"
#include "allocator.h"
#include "error-handling.h"
#include "safe-math.h"

/** Return a _temporary_ null-terminated version of the given string.

  @param secondary True if the second allocator slot should be used.

  @return Terminated string which will be invalidated by the following
  events:
    * Another call to this function with the same `secondary` value
    * Deletion or reallocation of the specified string
*/
static const char *internalNullTerminate(StringView string,
                                         const bool secondary)
{
  static Allocator *same_buffer_allocators[2] = { NULL, NULL };
  if(same_buffer_allocators[secondary] == NULL)
  {
    same_buffer_allocators[secondary] =
      allocatorWrapOneSingleGrowableBuffer(CR_GetGlobalRegion());
  }
  return strGetContent(string, same_buffer_allocators[secondary]);
}
static const char *nullTerminate(StringView string)
{
  return internalNullTerminate(string, false);
}
static const char *nullTerminateSecondary(StringView string)
{
  return internalNullTerminate(string, true);
}

struct FileStream
{
  FILE *file;

  /** The full or relative path representing the open stream. Needed for
    simplified printing of error messages. */
  StringView path;
};

/** Wraps the given arguments in a new FileStream struct.

  @param file The FILE stream which should be wrapped.
  @param path The path which should be wrapped.

  @return A new FileStream, containing the arguments of this function. It
  must be freed by the caller using free().
*/
static FileStream *newFileStream(FILE *file, StringView path)
{
  FileStream *stream = sMalloc(sizeof *stream);
  stream->file = file;
  strSet(&stream->path, path);
  return stream;
}

/** Applies the given stat function on the specified path and terminates
  the program on errors.

  @param path A filepath.
  @param stat_fun A stat function like e.g. stat() or lstat().

  @return Informations about the given file.
*/
static struct stat safeStat(StringView path,
                            int (*stat_fun)(const char *, struct stat *))
{
  struct stat buffer;
  if(stat_fun(nullTerminate(path), &buffer) == -1)
  {
    dieErrno("failed to access \"%s\"", nullTerminate(path));
  }

  return buffer;
}

/** A safe wrapper around malloc().

  @param size A value larger than 0. Otherwise the program will be
  terminated.

  @return A pointer to the allocated memory. Must be freed by the caller
  using free().
*/
void *sMalloc(const size_t size)
{
  return allocate(allocatorWrapMalloc(), size);
}

/** A safe wrapper around realloc().

  @param ptr The pointer to the data, which should be reallocated. The data
  may be moved, so the caller shouldn't use this pointer anymore once this
  function returns.
  @param size A value larger than 0. Otherwise the program will be
  terminated.

  @return A pointer to the reallocated memory. Must be freed by the caller
  using free().
*/
void *sRealloc(void *ptr, const size_t size)
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

/** Safe wrapper around atexit(). */
void sAtexit(void (*function)(void))
{
  if(atexit(function) != 0)
  {
    die("failed to register function with atexit");
  }
}

/** Safe wrapper around fopen().

  @param path The path to the file which should be opened for reading. The
  returned FileStream will keep a reference to this path, so make sure not
  to free or to modify it unless the stream gets closed.

  @return A file stream that can be used for reading. Must be closed by the
  caller.
*/
FileStream *sFopenRead(StringView path)
{
  FILE *file = fopen(nullTerminate(path), "rb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for reading", nullTerminate(path));
  }

  return newFileStream(file, path);
}

/** Almost identical to sFopenRead(), but with the difference that the file
  gets opened for writing. */
FileStream *sFopenWrite(StringView path)
{
  FILE *file = fopen(nullTerminate(path), "wb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for writing", nullTerminate(path));
  }

  return newFileStream(file, path);
}

/** Safe wrapper around fread(). This function will terminate the program
  on failure. If the given size is larger than the remaining bytes in the
  file stream, it will also terminate the program.

  @param ptr The location for the data that should be read.
  @param size The amount of bytes to read.
  @param stream A FileStream.
*/
void sFread(void *ptr, const size_t size, FileStream *stream)
{
  if(fread(ptr, 1, size, stream->file) != size)
  {
    if(feof(stream->file))
    {
      die("reading \"%s\": reached end of file unexpectedly",
          nullTerminate(fDestroy(stream)));
    }
    else
    {
      dieErrno("IO error while reading \"%s\"",
               nullTerminate(fDestroy(stream)));
    }
  }
}

/** Safe wrapper around fwrite(). Counterpart to sFread(). */
void sFwrite(const void *ptr, const size_t size, FileStream *stream)
{
  if(fwrite(ptr, 1, size, stream->file) != size)
  {
    dieErrno("failed to write to \"%s\"", nullTerminate(fDestroy(stream)));
  }
}

/** Unsafe version of sFwrite().

  @return True on success, otherwise false.
*/
bool fWrite(const void *ptr, const size_t size, FileStream *stream)
{
  return fwrite(ptr, 1, size, stream->file) == size;
}

/** Flushes and synchronizes the given FileStreams buffer to disk without
  handling errors.

  @param stream The output stream which should be flushed. If the passed
  stream is an input stream, the behaviour is undefined.

  @return True on success and false on failure, in which case errno will be
  set by either fileno(), fflush() or fdatasync().
*/
bool fTodisk(FileStream *stream)
{
  const int descriptor = fileno(stream->file);

  return descriptor != -1 && fflush(stream->file) == 0 &&
    fdatasync(descriptor) == 0;
}

/** Safe wrapper around fclose().

  @param stream The stream that should be closed.
*/
void sFclose(FileStream *stream)
{
  FILE *file = stream->file;
  StringView path = stream->path;
  free(stream);

  if(fclose(file) != 0)
  {
    dieErrno("failed to close \"%s\"", nullTerminate(path));
  }
}

/** Destroys the given file stream without checking for errors. It does not
  modify errno.

  @param stream The stream to be destroyed. It should not be used once this
  function returns.

  @return The path from the stream. Should not be freed by the caller.
*/
StringView fDestroy(FileStream *stream)
{
  StringView path = stream->path;

  const int old_errno = errno;
  fclose(stream->file);
  errno = old_errno;

  free(stream);

  return path;
}

/** Safe wrapper around opendir().

  @param path The directories filepath.

  @return A pointer to a valid directory stream.
*/
DIR *sOpenDir(StringView path)
{
  DIR *dir = opendir(nullTerminate(path));
  if(dir == NULL)
  {
    dieErrno("failed to open directory \"%s\"", nullTerminate(path));
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
struct dirent *sReadDir(DIR *dir, StringView path)
{
  struct dirent *dir_entry;
  const int old_errno = errno;
  errno = 0;

  do
  {
    dir_entry = readdir(dir);

    if(dir_entry == NULL && errno != 0)
    {
      dieErrno("failed to read directory \"%s\"", nullTerminate(path));
    }
  } while(dir_entry != NULL && dir_entry->d_name[0] == '.' &&
          (dir_entry->d_name[1] == '\0' ||
           (dir_entry->d_name[1] == '.' && dir_entry->d_name[2] == '\0')));

  errno = old_errno;
  return dir_entry;
}

/** Checks if there are unread bytes left in the given stream.

  @param stream The stream to check.

  @return True if the given stream has unread bytes left. False if it has
  reached its end.
*/
bool sFbytesLeft(FileStream *stream)
{
  const int old_errno = errno;
  errno = 0;

  const int character = fgetc(stream->file);
  if(character == EOF && errno != 0 && errno != EBADF)
  {
    dieErrno("failed to check for remaining bytes in \"%s\"",
             nullTerminate(fDestroy(stream)));
  }
  errno = old_errno;

  if(ungetc(character, stream->file) != character)
  {
    die("failed to check for remaining bytes in \"%s\"",
        nullTerminate(fDestroy(stream)));
  }

  return character != EOF;
}

/** Safe wrapper around closedir().

  @param dir The directory stream that should be closed.
  @param path The directory path of the given stream. Needed for printing
  useful error messages.
*/
void sCloseDir(DIR *dir, StringView path)
{
  if(closedir(dir) != 0)
  {
    dieErrno("failed to close directory \"%s\"", nullTerminate(path));
  }
}

/** Returns true if the given filepath exists and terminates the program
  on any unexpected errors.

  @param path The path to the file which existence should be checked.

  @return True if the path exists, false if not.
*/
bool sPathExists(StringView path)
{
  const int old_errno = errno;
  bool exists = true;
  struct stat stats;
  errno = 0;

  if(lstat(nullTerminate(path), &stats) != 0)
  {
    if(errno != 0 && errno != ENOENT)
    {
      dieErrno("failed to check existence of \"%s\"", nullTerminate(path));
    }

    exists = false;
  }

  errno = old_errno;
  return exists;
}

/** Safe wrapper around stat(). */
struct stat sStat(StringView path)
{
  return safeStat(path, stat);
}

/** Safe wrapper around lstat(). */
struct stat sLStat(StringView path)
{
  return safeStat(path, lstat);
}

/** Safe wrapper around mkdir(). */
void sMkdir(StringView path)
{
  if(mkdir(nullTerminate(path), 0755) != 0)
  {
    dieErrno("failed to create directory: \"%s\"", nullTerminate(path));
  }
}

/** A safe wrapper around symlink().

  @param target The path to which the symlink should point.
  @param path The path to the symlink to create.
*/
void sSymlink(StringView target, StringView path)
{
  if(symlink(nullTerminate(target), nullTerminateSecondary(path)) != 0)
  {
    dieErrno("failed to create symlink: \"%s\"", nullTerminate(path));
  }
}

/** Safe wrapper around rename(). */
void sRename(StringView oldpath, StringView newpath)
{
  if(rename(nullTerminate(oldpath), nullTerminateSecondary(newpath)) != 0)
  {
    dieErrno("failed to rename \"%s\" to \"%s\"", nullTerminate(oldpath),
             nullTerminateSecondary(newpath));
  }
}

/** Safe wrapper around chmod(). */
void sChmod(StringView path, const mode_t mode)
{
  if(chmod(nullTerminate(path), mode) != 0)
  {
    dieErrno("failed to change permissions of \"%s\"",
             nullTerminate(path));
  }
}

/** Safe wrapper around chown(). */
void sChown(StringView path, const uid_t user, const gid_t group)
{
  if(chown(nullTerminate(path), user, group) != 0)
  {
    dieErrno("failed to change owner of \"%s\"", nullTerminate(path));
  }
}

/** Safe wrapper around lchown(). */
void sLChown(StringView path, const uid_t user, const gid_t group)
{
  if(lchown(nullTerminate(path), user, group) != 0)
  {
    dieErrno("failed to change owner of \"%s\"", nullTerminate(path));
  }
}

/** Simplified safe wrapper around utime(). */
void sUtime(StringView path, const time_t time)
{
  const struct utimbuf time_buffer = {
    .actime = time,
    .modtime = time,
  };

  if(utime(nullTerminate(path), &time_buffer) != 0)
  {
    dieErrno("failed to set timestamp of \"%s\"", nullTerminate(path));
  }
}

/** Safe wrapper around remove(). */
void sRemove(StringView path)
{
  if(remove(nullTerminate(path)) != 0)
  {
    dieErrno("failed to remove \"%s\"", nullTerminate(path));
  }
}

bool alwaysReturnTrue(StringView path, const struct stat *stats,
                      void *user_data)
{
  (void)path;
  (void)stats;
  (void)user_data;

  return true;
}

void sRemoveRecursively(StringView path)
{
  sRemoveRecursivelyIf(path, alwaysReturnTrue, NULL);
}

typedef struct
{
  StringView current_path;
  ShouldRemoveCallback *should_remove;
  void *callback_user_data;

  /* Wrappers around single reusable buffers. See
     allocatorWrapOneSingleGrowableBuffer(). */
  Allocator *current_path_buffer;
  Allocator *tmp_buffer;
} RemoveRecursiveContext;

static bool removeRecursively(RemoveRecursiveContext *ctx)
{
  bool current_path_is_needed = false;

  const struct stat stats = sLStat(ctx->current_path);
  if(S_ISDIR(stats.st_mode))
  {
    DIR *dir = sOpenDir(ctx->current_path);
    for(const struct dirent *dir_entry = sReadDir(dir, ctx->current_path);
        dir_entry != NULL; dir_entry = sReadDir(dir, ctx->current_path))
    {
      StringView subpath = strAppendPath(
        ctx->current_path, str(dir_entry->d_name), ctx->tmp_buffer);

      strSet(&ctx->current_path,
             strCopy(subpath, ctx->current_path_buffer));
      if(!removeRecursively(ctx))
      {
        current_path_is_needed = true;
      }
      strSet(&ctx->current_path, strSplitPath(ctx->current_path).head);
    }
    sCloseDir(dir, ctx->current_path);
  }

  if(!current_path_is_needed &&
     ctx->should_remove(ctx->current_path, &stats,
                        ctx->callback_user_data))
  {
    sRemove(ctx->current_path);
    return true;
  }
  return false;
}

/** Recursively delete everything which doesn't pass the given check. Does
  not follow symlinks.

  @param path Item to be removed. Can also be a file or symlink.
  @param should_remove Will be called for the following items to check if
  they should be removed:
    * Regular files and symlinks
    * Empty directories
    * Directories which became empty after deletion
  Will never be called on non-empty directories.
  @param user_data Will be passed to each call of `should_remove`.
*/
void sRemoveRecursivelyIf(StringView path,
                          ShouldRemoveCallback should_remove,
                          void *user_data)
{
  CR_Region *r = CR_RegionNew();
  RemoveRecursiveContext ctx = {
    .current_path = path,
    .should_remove = should_remove,
    .callback_user_data = user_data,
    .current_path_buffer = allocatorWrapOneSingleGrowableBuffer(r),
    .tmp_buffer = allocatorWrapOneSingleGrowableBuffer(r),
  };
  removeRecursively(&ctx);
  CR_RegionRelease(r);
}

/** Safe and simplified wrapper around getcwd().

  @return The current working directory. Must be freed by the caller using
  free().
*/
char *sGetCwd(void)
{
  size_t capacity = 4;
  char *buffer = sMalloc(capacity);
  const int old_errno = errno;

  char *result = NULL;
  do
  {
    errno = 0;
    result = getcwd(buffer, capacity);
    if(result == NULL)
    {
      if(errno != ERANGE)
      {
        free(buffer);
        dieErrno("failed to determine current working directory");
      }

      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
    }
  } while(result == NULL);

  errno = old_errno;
  return buffer;
}

/** Reads a line from the given stream and terminates the program on
  failure.

  @param stream The stream to read from.

  @return A new string that must be freed by the caller using free().
  Returns NULL if the given stream has reached EOF.
*/
char *sReadLine(FILE *stream)
{
  size_t capacity = 16;
  size_t used = 0;
  char *buffer = sMalloc(capacity);

  const int old_errno = errno;
  errno = 0;

  bool reached_end = false;
  do
  {
    if(used == capacity)
    {
      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
    }

    const int character = fgetc(stream);
    if(character == '\n' || character == '\r' || character == '\0')
    {
      reached_end = true;
    }
    else if(character == EOF)
    {
      if(errno != 0)
      {
        free(buffer);
        dieErrno("failed to read line");
      }
      else if(used == 0)
      {
        free(buffer);
        buffer = NULL;
      }

      reached_end = true;
    }
    else
    {
      buffer[used] = (char)character;
      used++;
    }
  } while(!reached_end);
  errno = old_errno;

  if(buffer) buffer[used] = '\0';
  return buffer;
}

/** Check if the given file stream belongs to a terminal. */
bool sIsTTY(FILE *stream)
{
  const int descriptor = fileno(stream);
  if(descriptor == -1)
  {
    dieErrno("failed to get file descriptor from stream");
  }

  const int old_errno = errno;
  const int is_tty = isatty(descriptor);
  errno = old_errno;

  return is_tty == 1;
}

/** Converts the given string to a size_t value and terminates the program
  on conversion errors. */
size_t sStringToSize(StringView string)
{
  const int old_errno = errno;
  errno = 0;

  char *endptr;
  const long long int value = strtoll(nullTerminate(string), &endptr, 10);

  if(endptr == nullTerminate(string))
  {
    die("unable to convert to size: \"%s\"", nullTerminate(string));
  }
  else if(value < 0)
  {
    die("unable to convert negative value to size: \"%s\"",
        nullTerminate(string));
  }
  else if((value == LLONG_MAX && errno == ERANGE) ||
          (unsigned long long int)value > SIZE_MAX)
  {
    die("value too large to convert to size: \"%s\"",
        nullTerminate(string));
  }

  errno = old_errno;
  return (size_t)value;
}

/** Safe wrapper around time().

  @return The current time in seconds since 1970.
*/
time_t sTime(void)
{
  const time_t current_time = time(NULL);

  if(current_time == (time_t)-1)
  {
    die("failed to determine current time");
  }

  return current_time;
}

/** Wrapper around rand() which seeds srand() the first time its called. */
int sRand(void)
{
  static bool already_seeded = false;

  if(!already_seeded)
  {
    srand((sTime() << 9) + getpid());
    already_seeded = true;
  }

  return rand();
}

/** Reads an entire file into memory.

  @param region Region to use for allocating the required memory.
  @param path The path to the file.

  @return Contents of the requested file which should not be freed by the
  caller.
*/
FileContent sGetFilesContent(CR_Region *region, StringView path)
{
  const struct stat file_stats = sStat(path);
  if(!S_ISREG(file_stats.st_mode))
  {
    die("\"%s\" is not a regular file", nullTerminate(path));
  }

  /* On 32-bit systems off_t is often larger than size_t. */
  if((uint64_t)file_stats.st_size > SIZE_MAX)
  {
    die("unable to load file into mem due to its size: \"%s\"",
        nullTerminate(path));
  }

  if(file_stats.st_size > 0)
  {
    FileStream *stream = sFopenRead(path);
    char *content = CR_RegionAllocUnaligned(region, file_stats.st_size);
    sFread(content, file_stats.st_size, stream);
    const bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      die("file changed while reading: \"%s\"", nullTerminate(path));
    }

    return (FileContent){ .content = content, .size = file_stats.st_size };
  }

  char *content = CR_RegionAllocUnaligned(region, 1);
  content[0] = '\0';

  return (FileContent){ .content = content, .size = 0 };
}
