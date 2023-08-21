#include "safe-wrappers.h"

#include <errno.h>
#include <fcntl.h>
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

/** Returns a single reusable buffer allocator. Each allocation trough this
  allocator will invalidate all previously allocated memory from it.

  @param secondary True if the second slot should be used. Needed for
  allocating two temporary helper buffers.
*/
static Allocator *getTemporaryBuffer(const bool secondary)
{
  static Allocator *buffer[2] = { NULL, NULL };
  if(buffer[secondary] == NULL)
  {
    buffer[secondary] =
      allocatorWrapOneSingleGrowableBuffer(CR_GetGlobalRegion());
  }
  return buffer[secondary];
}

/** Return a _temporary_ null-terminated version of the given string.

  @return Terminated string which will be invalidated by the following
  events:
    * Another call to this function
    * Deletion or reallocation of the specified string
*/
static const char *nullTerminate(StringView string)
{
  return strGetContent(string, getTemporaryBuffer(false));
}
/** Like above, but uses a different reusable buffer. */
static const char *nullTerminateSecondary(StringView string)
{
  return strGetContent(string, getTemporaryBuffer(true));
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
    dieErrno("failed to access \"" PRI_STR "\"", STR_FMT(path));
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

/** Safe wrapper around atexit(). */
void sAtexit(void (*function)(void))
{
  if(atexit(function) != 0)
  {
    die("failed to register function with atexit");
  }
}

struct FileStream
{
  CR_Region *r;
  StringView path; /**< Used for error printing. */
  FILE *handle;
};

static FileStream *newFileStream(StringView path)
{
  CR_Region *r = CR_RegionNew();

  FileStream *stream = CR_RegionAlloc(r, sizeof *stream);
  stream->r = r;
  strSet(&stream->path, strCopy(path, allocatorWrapRegion(r)));
  return stream;
}
static void closeFileHandle(void *data)
{
  FileStream *stream = data;
  if(stream->handle == NULL) return;

  const int old_errno = errno;
  (void)fclose(stream->handle);
  errno = old_errno;
}

/** Safe wrapper around fopen().

  @param path The path to the file which should be opened for reading.

  @return A file stream that can be used for reading. Must be closed by the
  caller.
*/
FileStream *sFopenRead(StringView path)
{
  FileStream *result = newFileStream(path);
  result->handle = fopen(nullTerminate(path), "rb");
  if(result->handle == NULL)
  {
    CR_RegionRelease(result->r);
    dieErrno("failed to open \"" PRI_STR "\" for reading", STR_FMT(path));
  }
  CR_RegionAttach(result->r, closeFileHandle, result);

  return result;
}

/** Almost identical to sFopenRead(), but with the difference that the file
  gets opened for writing. */
FileStream *sFopenWrite(StringView path)
{
  FileStream *result = newFileStream(path);
  result->handle = fopen(nullTerminate(path), "wb");
  if(result->handle == NULL)
  {
    CR_RegionRelease(result->r);
    dieErrno("failed to open \"" PRI_STR "\" for writing", STR_FMT(path));
  }
  CR_RegionAttach(result->r, closeFileHandle, result);

  return result;
}

/** Returns a temporary, single-use copy of its internal string. */
static const char *internalFDestroy(FileStream *stream)
{
  char *result = strCopyRaw(stream->path, getTemporaryBuffer(false));
  CR_RegionRelease(stream->r);
  return result;
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
  if(fread(ptr, 1, size, stream->handle) != size)
  {
    if(feof(stream->handle))
    {
      die("reading \"%s\": reached end of file unexpectedly",
          internalFDestroy(stream));
    }
    else
    {
      dieErrno("IO error while reading \"%s\"", internalFDestroy(stream));
    }
  }
}

/** Safe wrapper around fwrite(). Counterpart to sFread(). */
void sFwrite(const void *ptr, const size_t size, FileStream *stream)
{
  if(fwrite(ptr, 1, size, stream->handle) != size)
  {
    dieErrno("failed to write to \"%s\"", internalFDestroy(stream));
  }
}

/** Unsafe version of sFwrite().

  @return True on success, otherwise false.
*/
bool fWrite(const void *ptr, const size_t size, FileStream *stream)
{
  return fwrite(ptr, 1, size, stream->handle) == size;
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
  const int descriptor = fileno(stream->handle);

  return descriptor != -1 && fflush(stream->handle) == 0 &&
    fdatasync(descriptor) == 0;
}

void fDatasync(StringView path)
{
  const int dir_descriptor = open(nullTerminate(path), O_RDONLY, 0);
  if(dir_descriptor == -1 || fdatasync(dir_descriptor) != 0 ||
     close(dir_descriptor) != 0)
  {
    dieErrno("failed to sync path to device: \"" PRI_STR "\"",
             STR_FMT(path));
  }
}

/** Safe wrapper around fclose().

  @param stream The stream that should be closed.
*/
void sFclose(FileStream *stream)
{
  FILE *handle = stream->handle;
  stream->handle = NULL;
  const char *path = internalFDestroy(stream);

  if(fclose(handle) != 0)
  {
    dieErrno("failed to close \"%s\"", path);
  }
}

/** Destroys the given file stream without checking for errors. It does not
  modify errno. */
void fDestroy(FileStream *stream)
{
  internalFDestroy(stream);
}

struct DirIterator
{
  CR_Region *r;
  StringView directory_path;
  Allocator *returned_result_buffer;
  DIR *handle;
};
void closeDirHandle(void *data)
{
  DirIterator *dir = data;
  if(dir->handle == NULL) return;

  const int old_errno = errno;
  (void)closedir(dir->handle);
  errno = old_errno;
}

/** @return Must be freed with sDirClose(). */
DirIterator *sDirOpen(StringView path)
{
  CR_Region *r = CR_RegionNew();
  DirIterator *dir = CR_RegionAlloc(r, sizeof *dir);

  dir->r = r;
  strSet(&dir->directory_path, strCopy(path, allocatorWrapRegion(r)));
  dir->returned_result_buffer = allocatorWrapOneSingleGrowableBuffer(r);
  dir->handle = opendir(nullTerminate(path));

  if(dir->handle == NULL)
  {
    CR_RegionRelease(r);
    dieErrno("failed to open directory \"" PRI_STR "\"", STR_FMT(path));
  }
  CR_RegionAttach(r, closeDirHandle, dir);

  return dir;
}

/** @return Empty string if the directory has reached its end. Otherwise it
  will return a full, absolute filepath which will be invalidated on the
  next call to sDirGetNext() or sDirClose(). */
StringView sDirGetNext(DirIterator *dir)
{
  struct dirent *dir_entry;
  const int old_errno = errno;
  errno = 0;

  do
  {
    dir_entry = readdir(dir->handle);

    if(dir_entry == NULL && errno != 0)
    {
      dieErrno("failed to read directory \"" PRI_STR "\"",
               STR_FMT(dir->directory_path));
    }
  } while(dir_entry != NULL && dir_entry->d_name[0] == '.' &&
          (dir_entry->d_name[1] == '\0' ||
           (dir_entry->d_name[1] == '.' && dir_entry->d_name[2] == '\0')));

  errno = old_errno;
  if(dir_entry == NULL)
  {
    return str("");
  }

  return strAppendPath(dir->directory_path, str(dir_entry->d_name),
                       dir->returned_result_buffer);
}

void sDirClose(DirIterator *dir)
{
  DIR *handle = dir->handle;
  dir->handle = NULL;

  if(closedir(handle) != 0)
  {
    dieErrno("failed to close directory \"" PRI_STR "\"",
             STR_FMT(dir->directory_path));
  }
  CR_RegionRelease(dir->r);
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

  const int character = fgetc(stream->handle);
  if(character == EOF && errno != 0 && errno != EBADF)
  {
    dieErrno("failed to check for remaining bytes in \"%s\"",
             internalFDestroy(stream));
  }
  errno = old_errno;

  if(ungetc(character, stream->handle) != character)
  {
    die("failed to check for remaining bytes in \"%s\"",
        internalFDestroy(stream));
  }

  return character != EOF;
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
      dieErrno("failed to check existence of \"" PRI_STR "\"",
               STR_FMT(path));
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
    dieErrno("failed to create directory: \"" PRI_STR "\"", STR_FMT(path));
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
    dieErrno("failed to create symlink: \"" PRI_STR "\"", STR_FMT(path));
  }
}

StringView sSymlinkReadTarget(StringView path, Allocator *a)
{
  const struct stat stats = sLStat(path);

  const uint64_t buffer_length = sUint64Add(stats.st_size, 1);
  if(buffer_length > SIZE_MAX)
  {
    die("symlink does not fit in memory: \"" PRI_STR "\"", STR_FMT(path));
  }
  else if(buffer_length > SSIZE_MAX)
  {
    /* In this case the behaviour of readlink() is implementation
       dependent and not portable. */
    die("symlink is too large: \"" PRI_STR "\"", STR_FMT(path));
  }

  char *buffer = allocate(a, buffer_length);

  /* Although st_size bytes are enough to store the symlinks target path,
     the full buffer is used. This allows to detect whether the symlink
     has increased in size while reading. */
  const ssize_t read_bytes =
    readlink(nullTerminate(path), buffer, buffer_length);
  if(read_bytes == -1)
  {
    dieErrno("failed to read symlink: \"" PRI_STR "\"", STR_FMT(path));
  }
  else if(read_bytes != stats.st_size)
  {
    die("symlink changed while reading: \"" PRI_STR "\"", STR_FMT(path));
  }
  buffer[read_bytes] = '\0';

  return str(buffer);
}

/** Safe wrapper around rename(). */
void sRename(StringView oldpath, StringView newpath)
{
  if(rename(nullTerminate(oldpath), nullTerminateSecondary(newpath)) != 0)
  {
    dieErrno("failed to rename \"" PRI_STR "\" to \"" PRI_STR "\"",
             STR_FMT(oldpath), STR_FMT(newpath));
  }
}

/** Safe wrapper around chmod(). */
void sChmod(StringView path, const mode_t mode)
{
  if(chmod(nullTerminate(path), mode) != 0)
  {
    dieErrno("failed to change permissions of \"" PRI_STR "\"",
             STR_FMT(path));
  }
}

/** Safe wrapper around chown(). */
void sChown(StringView path, const uid_t user, const gid_t group)
{
  if(chown(nullTerminate(path), user, group) != 0)
  {
    dieErrno("failed to change owner of \"" PRI_STR "\"", STR_FMT(path));
  }
}

/** Safe wrapper around lchown(). */
void sLChown(StringView path, const uid_t user, const gid_t group)
{
  if(lchown(nullTerminate(path), user, group) != 0)
  {
    dieErrno("failed to change owner of \"" PRI_STR "\"", STR_FMT(path));
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
    dieErrno("failed to set timestamp of \"" PRI_STR "\"", STR_FMT(path));
  }
}

void sRemove(StringView path)
{
  const int old_errno = errno;
  if(remove(nullTerminate(path)) != 0)
  {
    dieErrno("failed to remove \"" PRI_STR "\"", STR_FMT(path));
  }
  errno = old_errno;
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

static bool removeRecursivelyIf(StringView path,
                                ShouldRemoveCallback should_remove,
                                void *user_data)
{
  bool current_path_is_needed = false;

  const struct stat stats = sLStat(path);
  if(S_ISDIR(stats.st_mode))
  {
    DirIterator *dir = sDirOpen(path);
    for(StringView subpath = sDirGetNext(dir); !strIsEmpty(subpath);
        strSet(&subpath, sDirGetNext(dir)))
    {
      if(!removeRecursivelyIf(subpath, should_remove, user_data))
      {
        current_path_is_needed = true;
      }
    }
    sDirClose(dir);
  }

  if(!current_path_is_needed && should_remove(path, &stats, user_data))
  {
    sRemove(path);
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
  (void)removeRecursivelyIf(path, should_remove, user_data);
}

StringView sGetCurrentDir(Allocator *a)
{
  const int old_errno = errno;
  size_t capacity = 4;
  char *buffer = allocate(getTemporaryBuffer(false), capacity);

  while(true)
  {
    errno = 0;
    if(getcwd(buffer, capacity) != NULL)
    {
      errno = old_errno;
      return strCopy(str(buffer), a);
    }
    if(errno != ERANGE)
    {
      dieErrno("failed to determine current working directory");
    }

    capacity = sSizeMul(capacity, 2);
    buffer = allocate(getTemporaryBuffer(false), capacity);
  }
}

/** All non-EOF related errors will be handled by terminating the program
  with an error message.

  @param a Will be used to allocate the memory in `result_out`.
  @param result_out Will contain the read line. Will be set to an empty
  string if this function returns false.

  @return True on success. False when called on an EOF stream.
*/
bool sReadLine(FILE *stream, Allocator *a, StringView *result_out)
{
  const int old_errno = errno;

  size_t capacity = 4; /* Small value to test growth. */
  char *buffer = allocate(getTemporaryBuffer(false), capacity);
  size_t length_used = 0;

  while(true)
  {
    errno = 0;
    const int character = fgetc(stream);
    if(character == EOF)
    {
      if(errno != 0) dieErrno("failed to read line");

      if(length_used == 0)
      {
        strSet(result_out, str(""));
        errno = old_errno;
        return false;
      }
    }
    if(character == EOF || character == '\n' || character == '\r' ||
       character == '\0')
    {
      strSet(result_out, strCopy(strUnterminated(buffer, length_used), a));
      errno = old_errno;
      return true;
    }

    buffer[length_used] = (char)character;
    length_used++;

    if(length_used == capacity)
    {
      capacity = sSizeMul(capacity, 2);
      buffer = allocate(getTemporaryBuffer(false), capacity);
    }
  }
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

  const char *raw_string = nullTerminate(string);
  char *endptr;
  const long long int value = strtoll(raw_string, &endptr, 10);

  if(endptr == raw_string)
  {
    die("unable to convert to size: \"%s\"", raw_string);
  }
  else if(value < 0)
  {
    die("unable to convert negative value to size: \"%s\"", raw_string);
  }
  else if((value == LLONG_MAX && errno == ERANGE) ||
          (unsigned long long int)value > SIZE_MAX)
  {
    die("value too large to convert to size: \"%s\"", raw_string);
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

uint64_t sTimeMilliseconds(void)
{
  struct timespec timespec;
  if(clock_gettime(CLOCK_REALTIME, &timespec) == -1)
  {
    dieErrno("failed to determine current time");
  }

  return sUint64Add(sUint64Mul(timespec.tv_sec, 1000),
                    timespec.tv_nsec / 1000000);
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
    die("\"" PRI_STR "\" is not a regular file", STR_FMT(path));
  }

  /* On 32-bit systems off_t is often larger than size_t. */
  if((uint64_t)file_stats.st_size > SIZE_MAX)
  {
    die("unable to load file into mem due to its size: \"" PRI_STR "\"",
        STR_FMT(path));
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
      die("file changed while reading: \"" PRI_STR "\"", STR_FMT(path));
    }

    return (FileContent){ .content = content, .size = file_stats.st_size };
  }

  char *content = CR_RegionAllocUnaligned(region, 1);
  content[0] = '\0';

  return (FileContent){ .content = content, .size = 0 };
}

static void releaseRegex(void *data)
{
  regfree(data);
}

/** Compile the given regular expression and terminate the program on
  errors.

  @param file_name Name of the file containing the regular expression. Used
  for logging an error message.
  @param line_nr Line number in the file at which the regular expression
  was found. Needed for printing useful error messages.
*/
const regex_t *sRegexCompile(CR_Region *r, StringView expression,
                             StringView file_name, const size_t line_nr)
{
  regex_t *regex = CR_RegionAlloc(r, sizeof *regex);
  const int error =
    regcomp(regex, nullTerminate(expression), REG_EXTENDED | REG_NOSUB);

  if(error != 0)
  {
    const size_t error_length = regerror(error, regex, NULL, 0);
    char *error_str = allocate(getTemporaryBuffer(false), error_length);
    regerror(error, regex, error_str, error_length);
    die(PRI_STR ": line %zu: %s: \"" PRI_STR "\"", STR_FMT(file_name),
        line_nr, error_str, STR_FMT(expression));
  }
  CR_RegionAttach(r, releaseRegex, regex);

  return regex;
}

bool sRegexIsMatching(const regex_t *regex, StringView string)
{
  return regexec(regex, nullTerminate(string), 0, NULL, 0) == 0;
}
