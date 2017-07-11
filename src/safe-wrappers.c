/** @file
  Implements various wrapper functions, which handle errors by terminating
  with an error message.
*/

#include "safe-wrappers.h"

#include <errno.h>
#include <stdio.h>
#include <utime.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "path-builder.h"
#include "error-handling.h"

struct FileStream
{
  /** The actual FILE pointer wrapped by this struct. */
  FILE *file;

  /** The full or relative path representing the open stream. Needed for
    simplified printing of error messages. */
  const char *path;
};

/** Wraps the given arguments in a new FileStream struct.

  @param file The FILE stream which should be wrapped.
  @param path The path which should be wrapped.

  @return A new FileStream, containing the arguments of this function. It
  must be freed by the caller using free().
*/
static FileStream *newFileStream(FILE *file, const char *path)
{
  FileStream *stream = sMalloc(sizeof *stream);
  stream->file = file;
  stream->path = path;
  return stream;
}

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

/** Adds two uint64_t values and terminates the program on overflows.

  @param a The first summand.
  @param b The second summand.

  @return The sum of a and b.
*/
uint64_t sUint64Add(uint64_t a, uint64_t b)
{
  if(a > UINT64_MAX - b)
  {
    die("overflow calculating unsigned 64-bit value");
  }

  return a + b;
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
FileStream *sFopenRead(const char *path)
{
  FILE *file = fopen(path, "rb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for reading", path);
  }

  return newFileStream(file, path);
}

/** Almost identical to sFopenRead(), but with the difference that the file
  gets opened for writing. */
FileStream *sFopenWrite(const char *path)
{
  FILE *file = fopen(path, "wb");
  if(file == NULL)
  {
    dieErrno("failed to open \"%s\" for writing", path);
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
void sFread(void *ptr, size_t size, FileStream *stream)
{
  if(fread(ptr, 1, size, stream->file) != size)
  {
    if(feof(stream->file))
    {
      die("reading \"%s\": reached end of file unexpectedly",
          Fdestroy(stream));
    }
    else
    {
      dieErrno("IO error while reading \"%s\"", Fdestroy(stream));
    }
  }
}

/** Safe wrapper around fwrite(). Counterpart to sFread(). */
void sFwrite(const void *ptr, size_t size, FileStream *stream)
{
  if(fwrite(ptr, 1, size, stream->file) != size)
  {
    dieErrno("failed to write to \"%s\"", Fdestroy(stream));
  }
}

/** Unsafe version of sFwrite().

  @return True on success, otherwise false.
*/
bool Fwrite(const void *ptr, size_t size, FileStream *stream)
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
bool Ftodisk(FileStream *stream)
{
  int descriptor = fileno(stream->file);

  return descriptor != -1 &&
    fflush(stream->file) == 0 &&
    fdatasync(descriptor) == 0;
}

/** Safe wrapper around fclose().

  @param stream The stream that should be closed.
*/
void sFclose(FileStream *stream)
{
  FILE *file = stream->file;
  const char *path = stream->path;
  free(stream);

  if(fclose(file) != 0)
  {
    dieErrno("failed to close \"%s\"", path);
  }
}

/** Destroys the given file stream without checking for errors. It does not
  modify errno.

  @param stream The stream to be destroyed. It should not be used once this
  function returns.

  @return The path from the stream. Should not be freed by the caller.
*/
const char *Fdestroy(FileStream *stream)
{
  const char *path = stream->path;

  int old_errno = errno;
  fclose(stream->file);
  errno = old_errno;

  free(stream);

  return path;
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
  int old_errno = errno;
  errno = 0;

  do
  {
    dir_entry = readdir(dir);

    if(dir_entry == NULL && errno != 0)
    {
      dieErrno("failed to read directory \"%s\"", path);
    }
  }
  while(dir_entry != NULL &&
        dir_entry->d_name[0] == '.' &&
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
  int old_errno = errno;
  errno = 0;

  int character = fgetc(stream->file);
  if(character == EOF && errno != 0)
  {
    dieErrno("failed to check for remaining bytes in \"%s\"",
             Fdestroy(stream));
  }
  errno = old_errno;

  if(ungetc(character, stream->file) != character)
  {
    die("failed to check for remaining bytes in \"%s\"",
        Fdestroy(stream));
  }

  return character != EOF;
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
  bool exists = true;
  struct stat stats;
  errno = 0;

  if(lstat(path, &stats) != 0)
  {
    if(errno != 0 && errno != ENOENT)
    {
      dieErrno("failed to check existence of \"%s\"", path);
    }

    exists = false;
  }

  errno = old_errno;
  return exists;
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

/** A failsafe wrapper around mkdir().

  @param path The path to the directory to create.
*/
void sMkdir(const char *path)
{
  if(mkdir(path, 0755) != 0)
  {
    dieErrno("failed to create directory: \"%s\"", path);
  }
}

/** A failsafe wrapper around symlink().

  @param target The path to which the symlink should point.
  @param path The path to the symlink to create.
*/
void sSymlink(const char *target, const char *path)
{
  if(symlink(target, path) != 0)
  {
    dieErrno("failed to create symlink: \"%s\"", path);
  }
}

/** Safe wrapper around rename().

  @param oldpath Path to the file which should be renamed.
  @param newpath The new filepath.
*/
void sRename(const char *oldpath, const char *newpath)
{
  if(rename(oldpath, newpath) != 0)
  {
    dieErrno("failed to rename \"%s\" to \"%s\"", oldpath, newpath);
  }
}

/** Safe wrapper around chmod(). */
void sChmod(const char *path, mode_t mode)
{
  if(chmod(path, mode) != 0)
  {
    dieErrno("failed to change permissions of \"%s\"", path);
  }
}

/** Safe wrapper around chown(). */
void sChown(const char *path, uid_t user, gid_t group)
{
  if(chown(path, user, group) != 0)
  {
    dieErrno("failed to change owner of \"%s\"", path);
  }
}

/** Safe wrapper around lchown(). */
void sLChown(const char *path, uid_t user, gid_t group)
{
  if(lchown(path, user, group) != 0)
  {
    dieErrno("failed to change owner of \"%s\"", path);
  }
}

/** Simplified safe wrapper around utime(). */
void sUtime(const char *path, time_t time)
{
  struct utimbuf time_buffer =
  {
    .actime  = time,
    .modtime = time,
  };

  if(utime(path, &time_buffer) != 0)
  {
    dieErrno("failed to set timestamp of \"%s\"", path);
  }
}

/** Safe wrapper around remove().

  @param path The path to remove.
*/
void sRemove(const char *path)
{
  if(remove(path) != 0)
  {
    dieErrno("failed to remove \"%s\"", path);
  }
}

/** Removes the given path recursively.

  @param buffer The buffer containing the null-terminated path to remove.
  @param length The length of the given path.
*/
static void removeRecursively(Buffer *buffer, size_t length)
{
  struct stat stats = sLStat(buffer->data);

  if(S_ISDIR(stats.st_mode))
  {
    DIR *dir = sOpenDir(buffer->data);

    for(struct dirent *dir_entry = sReadDir(dir, buffer->data);
        dir_entry != NULL; dir_entry = sReadDir(dir, buffer->data))
    {
      size_t sub_path_length =
        pathBuilderAppend(&buffer, length, dir_entry->d_name);

      removeRecursively(buffer, sub_path_length);

      buffer->data[length] = '\0';
    }

    sCloseDir(dir, buffer->data);
  }

  sRemove(buffer->data);
}

/** Recursive version of sRemove(). */
void sRemoveRecursively(const char *path)
{
  static Buffer *buffer = NULL;
  size_t length = pathBuilderSet(&buffer, path);

  removeRecursively(buffer, length);
}

/** Safe and simplified wrapper around getcwd().

  @return The current working directory. Must be freed by the caller using
  free().
*/
char *sGetCwd(void)
{
  size_t capacity = 4;
  char *buffer = sMalloc(capacity);
  int old_errno = errno;

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
  }while(result == NULL);

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
  size_t used     = 0;
  char *buffer    = sMalloc(capacity);

  int old_errno = errno;
  errno = 0;

  bool reached_end = false;
  do
  {
    if(used == capacity)
    {
      capacity = sSizeMul(capacity, 2);
      buffer = sRealloc(buffer, capacity);
    }

    int character = fgetc(stream);
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
      buffer[used] = character;
      used++;
    }
  }while(reached_end == false);
  errno = old_errno;

  if(buffer) buffer[used] = '\0';
  return buffer;
}

/** Converts the given string to a size_t value and terminates the program
  on conversion errors.

  @param string The string to convert.

  @return The value represented by the given string.
*/
size_t sStringToSize(const char *string)
{
  int old_errno = errno;
  errno = 0;

  char *endptr;
  long long int value = strtoll(string, &endptr, 10);

  if(endptr == string)
  {
    die("unable to convert to size: \"%s\"", string);
  }
  else if(value < 0)
  {
    die("unable to convert negative value to size: \"%s\"", string);
  }
  else if((value == LLONG_MAX && errno == ERANGE) ||
          (unsigned long long int)value > SIZE_MAX)
  {
    die("value too large to convert to size: \"%s\"", string);
  }

  errno = old_errno;
  return (size_t)value;
}

/** Safe wrapper around time().

  @return The current time in seconds since 1970.
*/
time_t sTime(void)
{
  time_t current_time = time(NULL);

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

  if(already_seeded == false)
  {
    srand((sTime() << 9) + getpid());
    already_seeded = true;
  }

  return rand();
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

  /* On 32-bit systems off_t is often larger than size_t. */
  if((uint64_t)file_stats.st_size > SIZE_MAX)
  {
    die("unable to load file into mem due to its size: \"%s\"", path);
  }

  char *content = NULL;
  if(file_stats.st_size > 0)
  {
    FileStream *stream = sFopenRead(path);
    content = sMalloc(file_stats.st_size);
    sFread(content, file_stats.st_size, stream);
    bool stream_not_at_end = sFbytesLeft(stream);
    sFclose(stream);

    if(stream_not_at_end)
    {
      free(content);
      die("file changed while reading: \"%s\"", path);
    }
  }

  return (FileContent){ .content = content, .size = file_stats.st_size };
}
