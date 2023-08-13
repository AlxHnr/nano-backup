#include "repository.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "CRegion/alloc-growable.h"
#include "CRegion/region.h"

#include "error-handling.h"
#include "safe-math.h"
#include "safe-wrappers.h"

/** A struct for safely writing files into backup repositories. */
struct RepoWriter
{
  StringView repo_path;

  /** The path to the repositories temporary dummy file. */
  StringView repo_tmp_file_path;

  /** The path to the source file in the filesystem, which gets written
    to the repository trough this writer. This is required for printing
    useful error messages. */
  StringView source_file_path;

  /** The FileStream wrapped by this struct. */
  FileStream *stream;

  /** True, if the repo writer was opened in raw mode. */
  bool raw_mode;

  /** Contains informations about the final path to which the temporary
    file gets renamed to. */
  union
  {
    /** If the repo writer was opened in raw mode, it will contain the
      final path to which the temporary file will be renamed to. */
    StringView path;

    /** If the repo writer was not opened in raw mode, the final filepath
      will be generated from this file info. */
    const RegularFileInfo *info;
  } rename_to;
};

/** A struct for reading files from backup repositories. */
struct RepoReader
{
  StringView repo_path;

  /** The original filepath representing the file read trough this reader.
    This is required for printing useful error messages. */
  StringView source_file_path;

  /** The FILE stream wrapped by this struct. */
  FILE *stream;
};

/** Returns the required capacity to store the unique path of the given
  file info, including its terminating null byte. */
static size_t getFilePathRequiredCapacity(const RegularFileInfo *info)
{
  return snprintf(NULL, 0, "%x", info->slot) +
    snprintf(NULL, 0, "%" PRIx64, info->size) + (FILE_HASH_SIZE * 2) +
    5; /* 2 Slashes, 2 X's and '\0'. */
}

/** Writes the unique path of the given file info into the specified
  buffer. */
static void buildFilePath(char *buffer, const RegularFileInfo *info)
{
  sprintf(buffer, "%02x", info->hash[0]);
  sprintf(&buffer[3], "%02x", info->hash[1]);
  buffer[2] = buffer[1];
  buffer[1] = '/';
  buffer[5] = buffer[4];
  buffer[4] = '/';
  buffer += 2;

  for(size_t index = 2; index < FILE_HASH_SIZE; index++)
  {
    sprintf(&buffer[index * 2], "%02x", info->hash[index]);
  }

  char *suffix_buffer = &buffer[FILE_HASH_SIZE * 2];
  sprintf(suffix_buffer, "x%" PRIx64 "x%x", info->size, info->slot);
}

static char *path_buffer = NULL;

/** Populates the path_buffer with the path required for accessing a file
  inside the repository.

  @param repo_path The full or relative path to a backup repository.
  @param info The informations describing the file, for which the path
  should be build.
*/
static void fillPathBufferWithInfo(StringView repo_path,
                                   const RegularFileInfo *info)
{
  /* The +1 is for the slash after the repo path. */
  size_t required_capacity = getFilePathRequiredCapacity(info) + 1;
  required_capacity = sSizeAdd(required_capacity, repo_path.length);
  path_buffer = CR_EnsureCapacity(path_buffer, required_capacity);

  memcpy(path_buffer, repo_path.content, repo_path.length);
  path_buffer[repo_path.length] = '/';

  char *hash_buffer = &path_buffer[repo_path.length + 1];
  buildFilePath(hash_buffer, info);
}

static RepoWriter *createRepoWriter(StringView repo_path,
                                    StringView repo_tmp_file_path,
                                    StringView source_file_path,
                                    const bool raw_mode)
{
  FileStream *stream = sFopenWrite(repo_tmp_file_path);
  RepoWriter *writer = sMalloc(sizeof *writer);

  strSet(&writer->repo_path, repo_path);
  strSet(&writer->repo_tmp_file_path, repo_tmp_file_path);
  strSet(&writer->source_file_path, source_file_path);
  writer->stream = stream;
  writer->raw_mode = raw_mode;

  return writer;
}

/** Checks if a file with the given properties exist inside the specified
  repository.

  @param repo_path The full or relative path to a backup repository.
  @param info The file info describing the file inside the repository.

  @return True, if the given file exists.
*/
bool repoRegularFileExists(StringView repo_path,
                           const RegularFileInfo *info)
{
  fillPathBufferWithInfo(repo_path, info);
  return sPathExists(str(path_buffer));
}

/** Builds the unique path of the file represented by the given info.

  @param buffer_ptr Buffer for storing the string. This buffer must have
  been created by CR_RegionAllocGrowable() or must point to NULL otherwise.
  If it points to NULL, a new buffer will be allocated by
  CR_EnsureCapacity(), to which the given pointer will be assigned.
  @param info The info from which the filepath will be built.
*/
void repoBuildRegularFilePath(char **buffer_ptr,
                              const RegularFileInfo *info)
{
  const size_t required_capacity = getFilePathRequiredCapacity(info);
  *buffer_ptr = CR_EnsureCapacity(*buffer_ptr, required_capacity);
  buildFilePath(*buffer_ptr, info);
}

/** Opens a new RepoReader for reading a file from a repository.

  @param repo_path The path to the repository. The returned RepoReader will
  keep a reference to this string, so make sure not to modify or free it
  while the reader is in use.
  @param source_file_path The requested files original path. It is only
  used for printing useful error messages in case of failure. The returned
  RepoReader will keep a reference to this string, so make sure not to
  modify or free it while the reader is in use.
  @param info Informations about the requested file. Needed for generating
  the files unique name inside the repository.

  @return A new RepoReader which must be closed using repoReaderClose().
*/
RepoReader *repoReaderOpenFile(StringView repo_path,
                               StringView source_file_path,
                               const RegularFileInfo *info)
{
  fillPathBufferWithInfo(repo_path, info);
  FILE *stream = fopen(path_buffer, "rb");
  if(stream == NULL)
  {
    dieErrno("failed to open \"%s\" in \"%s\"", source_file_path.content,
             repo_path.content);
  }

  RepoReader *reader = sMalloc(sizeof *reader);
  strSet(&reader->repo_path, repo_path);
  strSet(&reader->source_file_path, source_file_path);
  reader->stream = stream;

  return reader;
}

/** Reads data from a RepoReader.

  @param data The location to store the requested data.
  @param size The number of bytes to read.
  @param reader The reader which should be used.
*/
void repoReaderRead(void *data, const size_t size, RepoReader *reader)
{
  if(fread(data, 1, size, reader->stream) != size)
  {
    StringView repo_path = reader->repo_path;
    StringView source_file_path = reader->source_file_path;
    const bool reached_end_of_file = feof(reader->stream);

    const int old_errno = errno;
    fclose(reader->stream);
    errno = old_errno;

    free(reader);

    if(reached_end_of_file)
    {
      die("reading \"%s\" from \"%s\": reached end of file unexpectedly",
          source_file_path.content, repo_path.content);
    }
    else
    {
      dieErrno("IO error while reading \"%s\" from \"%s\"",
               source_file_path.content, repo_path.content);
    }
  }
}

/** Closes the given RepoReader and frees all its memory.

  @param reader_to_close The reader which should be closed. It will be
  destroyed by this function and should not be used anymore.
*/
void repoReaderClose(RepoReader *reader_to_close)
{
  RepoReader reader = *reader_to_close;
  free(reader_to_close);

  if(fclose(reader.stream) != 0)
  {
    dieErrno("failed to close \"%s\" in \"%s\"",
             reader.source_file_path.content, reader.repo_path.content);
  }
}

/** Opens a new RepoWriter for safe writing into the specified repository.
  The returned RepoWriter will keep a reference to all the arguments passed
  to this function, so make sure not to free or modify them as long as the
  writer is in use. The caller of this function must ensure, that only one
  writer exists for the given repository. Otherwise it will lead to
  corrupted data.

  @param repo_path The path to the repository.
  @param repo_tmp_file_path The path to the dummy file inside the
  repository. This is the file to which all the data will be written. Once
  the writer gets closed, the data will be synced to disk and the dummy
  file gets renamed to the final file. If the dummy file already exists, it
  will be overwritten. The dummy file must be either inside the repository
  or on the same device as the repository in order to be effective.
  @param source_file_path The path to the original file, that gets written
  to the repository trough this writer. This is only needed in case of an
  error, to display which file failed to be written to the repository.
  @param info Informations describing the file, which gets written to the
  repository. This is needed for generating the filename inside the
  repository. All values inside this struct must be defined, otherwise it
  will lead to unexpected behaviour. So make sure, that the files size is
  larger than FILE_HASH_SIZE.

  @return A new RepoWriter, which must be closed by the caller using
  repoWriterClose().
*/
RepoWriter *repoWriterOpenFile(StringView repo_path,
                               StringView repo_tmp_file_path,
                               StringView source_file_path,
                               const RegularFileInfo *info)
{
  RepoWriter *writer = createRepoWriter(repo_path, repo_tmp_file_path,
                                        source_file_path, false);
  writer->rename_to.info = info;

  return writer;
}

/** Like repoWriterOpenFile(), but takes the final filepath as argument.

  @param final_path The path to which the temporary file gets renamed to
  after flushing. This file must be directly inside the repository. The
  returned RepoWriter will keep a reference to this string, so make sure
  not to modify or free it as long as the writer is in use.
*/
RepoWriter *repoWriterOpenRaw(StringView repo_path,
                              StringView repo_tmp_file_path,
                              StringView source_file_path,
                              StringView final_path)
{
  RepoWriter *writer = createRepoWriter(repo_path, repo_tmp_file_path,
                                        source_file_path, true);
  strSet(&writer->rename_to.path, final_path);

  return writer;
}

/** Writes data using the given RepoWriter and terminates the program on
  failure.

  @param data The data which should be written.
  @param size The size of the data in bytes.
  @param writer The writer which should be used.
*/
void repoWriterWrite(const void *data, const size_t size,
                     RepoWriter *writer)
{
  if(!fWrite(data, size, writer->stream))
  {
    StringView repo_path = writer->repo_path;
    StringView source_file_path = writer->source_file_path;

    fDestroy(writer->stream);
    free(writer);

    dieErrno("IO error while writing \"%s\" to \"%s\"",
             source_file_path.content, repo_path.content);
  }
}

/** Calls fdatasync() on the given directories descriptor.

  @param path The path to the directory to sync to disk.
*/
static void fdatasyncDirectory(StringView path)
{
  const int dir_descriptor = open(path.content, O_RDONLY, 0);
  if(dir_descriptor == -1 || fdatasync(dir_descriptor) != 0 ||
     close(dir_descriptor) != 0)
  {
    dieErrno("failed to sync directory to device: \"%s\"", path.content);
  }
}

/** Finalizes the write process represented by the given writer. All its
  data will be written to disk and the temporary file will be renamed to
  its final filename.

  @param writer_to_close The writer which should be finalized. This
  function will destroy the writer and free all memory associated with it.
*/
void repoWriterClose(RepoWriter *writer_to_close)
{
  RepoWriter writer = *writer_to_close;
  free(writer_to_close);

  if(!fTodisk(writer.stream))
  {
    fDestroy(writer.stream);
    dieErrno("failed to flush/sync \"%s\" to \"%s\"",
             writer.source_file_path.content, writer.repo_path.content);
  }

  sFclose(writer.stream);

  if(writer.raw_mode)
  {
    sRename(writer.repo_tmp_file_path, writer.rename_to.path);
  }
  else
  {
    StringView repo_path = writer.repo_path;
    fillPathBufferWithInfo(repo_path, writer.rename_to.info);

    /* Ensure that the final paths parent directories exists. */
    path_buffer[repo_path.length + 5] = '\0';
    if(!sPathExists(str(path_buffer)))
    {
      path_buffer[repo_path.length + 2] = '\0';
      if(!sPathExists(str(path_buffer)))
      {
        sMkdir(str(path_buffer));
        fdatasyncDirectory(writer.repo_path);
      }
      path_buffer[repo_path.length + 2] = '/';

      sMkdir(str(path_buffer));

      path_buffer[repo_path.length + 2] = '\0';
      fdatasyncDirectory(str(path_buffer));
      path_buffer[repo_path.length + 2] = '/';
    }
    path_buffer[repo_path.length + 5] = '/';

    sRename(writer.repo_tmp_file_path, str(path_buffer));
    path_buffer[repo_path.length + 5] = '\0';
    fdatasyncDirectory(str(path_buffer));
  }

  fdatasyncDirectory(writer.repo_path);
}

typedef struct
{
  /** True if the lockfile is locked by the current process. */
  bool is_locked;
  int file_descriptor;
  const char *file_path;
} LockfileInfo;

static void cleanupLockfile(void *lockfile_info_ptr)
{
  const LockfileInfo *info = lockfile_info_ptr;

  const int old_errno = errno;
  if(info->is_locked)
  {
    (void)lockf(info->file_descriptor, F_ULOCK, 0);
    (void)close(info->file_descriptor);
    (void)remove(info->file_path);
  }
  else
  {
    (void)close(info->file_descriptor);
  }
  errno = old_errno;
}

/** Lock the specified repository or terminate with an error message. The
  repository will stay locked until the program exits. Calling this
  function twice from the same process on the same repository will not
  fail.

  @param repo_path Either a relative or absolute path to the repository to
  lock.
*/
void repoLockUntilExit(StringView repo_path)
{
  /* Must be allocated before the region to outlive it. */
  StringView lockfile_path =
    strLegacyAppendPath(repo_path, str("lockfile"));

  CR_Region *r = CR_RegionNew();
  LockfileInfo *lockfile_info = CR_RegionAlloc(r, sizeof *lockfile_info);
  lockfile_info->is_locked = false;
  lockfile_info->file_path = lockfile_path.content;
  lockfile_info->file_descriptor =
    open(lockfile_info->file_path, O_CREAT | O_WRONLY, S_IWUSR | S_IWGRP);

  if(lockfile_info->file_descriptor == -1)
  {
    dieErrno("failed to create lockfile: \"%s\"",
             lockfile_info->file_path);
  }
  CR_RegionAttach(r, cleanupLockfile, lockfile_info);

  if(lockf(lockfile_info->file_descriptor, F_TLOCK, 0) == 0)
  {
    lockfile_info->is_locked = true;
  }
  else if(errno == EACCES || errno == EAGAIN)
  {
    die("repository is already being used by another process");
  }
  else
  {
    dieErrno("failed to lock repository: \"%s\"",
             lockfile_info->file_path);
  }
}
