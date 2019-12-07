/** @file
  Implements various helper functions to handle repositories.
*/

#include "repository.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <inttypes.h>

#include "safe-math.h"
#include "safe-wrappers.h"
#include "error-handling.h"

/** A struct for safely writing files into backup repositories. */
struct RepoWriter
{
  /** The path to the repository. */
  const char *repo_path;

  /** The path to the repositories temporary dummy file. */
  const char *repo_tmp_file_path;

  /** The path to the source file in the filesystem, which gets written
    to the repository trough this writer. This is required for printing
    useful error messages. */
  const char *source_file_path;

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
    const char *path;

    /** If the repo writer was not opened in raw mode, the final filepath
      will be generated from this file info. */
    const RegularFileInfo *info;
  }rename_to;
};

/** A struct for reading files from backup repositories. */
struct RepoReader
{
  /** The path to the repository. */
  const char *repo_path;

  /** The original filepath representing the file read trough this reader.
    This is required for printing useful error messages. */
  const char *source_file_path;

  /** The FILE stream wrapped by this struct. */
  FILE *stream;
};

/** Returns the required capacity to store the unique path of the given
  file info, including its terminating null byte. */
static size_t getFilePathRequiredCapacity(const RegularFileInfo *info)
{
  return
    snprintf(NULL, 0, "%x", info->slot) +
    snprintf(NULL, 0, "%"PRIx64, info->size) +
    (FILE_HASH_SIZE * 2) + 5; /* 2 Slashes, 2 X's and '\0'. */
}

/** Writes the unique path of the given file info into the specified
  buffer. */
static void buildFilePath(char *buffer, const RegularFileInfo *info)
{
  sprintf(buffer,     "%02x", info->hash[0]);
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
  sprintf(suffix_buffer, "x%"PRIx64"x%x", info->size, info->slot);
}

/** A reusable buffer for constructing paths of files inside repos. */
static Buffer *path_buffer = NULL;

/** Populates the path_buffer with the path required for accessing a file
  inside the repository.

  @param repo_path The full or relative path to a backup repository.
  @param info The informations describing the file, for which the path
  should be build.
*/
static void fillPathBufferWithInfo(String repo_path,
                                   const RegularFileInfo *info)
{
  /* The +1 is for the slash after the repo path. */
  size_t required_capacity = getFilePathRequiredCapacity(info) + 1;
  required_capacity = sSizeAdd(required_capacity, repo_path.length);
  bufferEnsureCapacity(&path_buffer, required_capacity);

  memcpy(path_buffer->data, repo_path.content, repo_path.length);
  path_buffer->data[repo_path.length] = '/';

  char *hash_buffer = &path_buffer->data[repo_path.length + 1];
  buildFilePath(hash_buffer, info);
}

/** Creates a new RepoWriter from its arguments. Contains the core logic
  behind repoWriterOpenFile() and repoWriterOpenRaw(). */
static RepoWriter *createRepoWriter(const char *repo_path,
                                    const char *repo_tmp_file_path,
                                    const char *source_file_path,
                                    bool raw_mode)
{
  FileStream *stream = sFopenWrite(repo_tmp_file_path);
  RepoWriter *writer = sMalloc(sizeof *writer);

  writer->repo_path = repo_path;
  writer->repo_tmp_file_path = repo_tmp_file_path;
  writer->source_file_path = source_file_path;
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
bool repoRegularFileExists(String repo_path, const RegularFileInfo *info)
{
  fillPathBufferWithInfo(repo_path, info);
  return sPathExists(path_buffer->data);
}

/** Builds the unique path of the file represented by the given info.

  @param buffer The buffer to which the string will be written.
  @param info The info from which the filepath will be built.
*/
void repoBuildRegularFilePath(Buffer **buffer, const RegularFileInfo *info)
{
  size_t required_capacity = getFilePathRequiredCapacity(info);
  bufferEnsureCapacity(buffer, required_capacity);
  buildFilePath((*buffer)->data, info);
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
RepoReader *repoReaderOpenFile(const char *repo_path,
                               const char *source_file_path,
                               const RegularFileInfo *info)
{
  fillPathBufferWithInfo(strWrap(repo_path), info);
  FILE *stream = fopen(path_buffer->data, "rb");
  if(stream == NULL)
  {
    dieErrno("failed to open \"%s\" in \"%s\"",
             source_file_path, repo_path);
  }

  RepoReader *reader = sMalloc(sizeof *reader);
  reader->repo_path = repo_path;
  reader->source_file_path = source_file_path;
  reader->stream = stream;

  return reader;
}

/** Reads data from a RepoReader.

  @param data The location to store the requested data.
  @param size The number of bytes to read.
  @param reader The reader which should be used.
*/
void repoReaderRead(void *data, size_t size, RepoReader *reader)
{
  if(fread(data, 1, size, reader->stream) != size)
  {
    const char *repo_path = reader->repo_path;
    const char *source_file_path = reader->source_file_path;
    bool reached_end_of_file = feof(reader->stream);

    int old_errno = errno;
    fclose(reader->stream);
    errno = old_errno;

    free(reader);

    if(reached_end_of_file)
    {
      die("reading \"%s\" from \"%s\": reached end of file unexpectedly",
          source_file_path, repo_path);
    }
    else
    {
      dieErrno("IO error while reading \"%s\" from \"%s\"",
               source_file_path, repo_path);
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
             reader.source_file_path, reader.repo_path);
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
RepoWriter *repoWriterOpenFile(const char *repo_path,
                               const char *repo_tmp_file_path,
                               const char *source_file_path,
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
RepoWriter *repoWriterOpenRaw(const char *repo_path,
                              const char *repo_tmp_file_path,
                              const char *source_file_path,
                              const char *final_path)
{
  RepoWriter *writer = createRepoWriter(repo_path, repo_tmp_file_path,
                                        source_file_path, true);
  writer->rename_to.path = final_path;

  return writer;
}

/** Writes data using the given RepoWriter and terminates the program on
  failure.

  @param data The data which should be written.
  @param size The size of the data in bytes.
  @param writer The writer which should be used.
*/
void repoWriterWrite(const void *data, size_t size, RepoWriter *writer)
{
  if(Fwrite(data, size, writer->stream) == false)
  {
    const char *repo_path = writer->repo_path;
    const char *source_file_path = writer->source_file_path;

    Fdestroy(writer->stream);
    free(writer);

    dieErrno("IO error while writing \"%s\" to \"%s\"",
             source_file_path, repo_path);
  }
}

/** Calls fdatasync() on the given directories descriptor.

  @param path The path to the directory to sync to disk.
*/
static void fdatasyncDirectory(const char *path)
{
  int dir_descriptor = open(path, O_RDONLY, 0);
  if(dir_descriptor == -1 ||
     fdatasync(dir_descriptor) != 0 ||
     close(dir_descriptor) != 0)
  {
    dieErrno("failed to sync directory to device: \"%s\"", path);
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

  if(Ftodisk(writer.stream) == false)
  {
    Fdestroy(writer.stream);
    dieErrno("failed to flush/sync \"%s\" to \"%s\"",
             writer.source_file_path, writer.repo_path);
  }

  sFclose(writer.stream);

  if(writer.raw_mode == true)
  {
    sRename(writer.repo_tmp_file_path, writer.rename_to.path);
  }
  else
  {
    String repo_path = strWrap(writer.repo_path);
    fillPathBufferWithInfo(repo_path, writer.rename_to.info);

    /* Ensure that the final paths parent directories exists. */
    path_buffer->data[repo_path.length + 5] = '\0';
    if(sPathExists(path_buffer->data) == false)
    {
      path_buffer->data[repo_path.length + 2] = '\0';
      if(sPathExists(path_buffer->data) == false)
      {
        sMkdir(path_buffer->data);
        fdatasyncDirectory(writer.repo_path);
      }
      path_buffer->data[repo_path.length + 2] = '/';

      sMkdir(path_buffer->data);

      path_buffer->data[repo_path.length + 2] = '\0';
      fdatasyncDirectory(path_buffer->data);
      path_buffer->data[repo_path.length + 2] = '/';
    }
    path_buffer->data[repo_path.length + 5] = '/';

    sRename(writer.repo_tmp_file_path, path_buffer->data);
    path_buffer->data[repo_path.length + 5] = '\0';
    fdatasyncDirectory(path_buffer->data);
  }

  fdatasyncDirectory(writer.repo_path);
}
