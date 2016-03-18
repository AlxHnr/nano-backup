/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

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

/** A reusable buffer for constructing paths of files inside repos. */
static char *path_buffer = NULL;
static size_t path_buffer_capacity = 0;

/** Frees the path buffer. */
static void freePathBuffer(void)
{
  free(path_buffer);
}

/** Ensures that the path_buffer has at least the required capacity.
  Otherwise it will be reallocated.

  @param size The size which the path_buffer must have.
*/
static void pathBufferEnsureCapacity(size_t size)
{
  if(size > path_buffer_capacity)
  {
    if(path_buffer == NULL) atexit(freePathBuffer);

    path_buffer = sRealloc(path_buffer, size);
    path_buffer_capacity = size;
  }
}

/** Populates the path_buffer with the path required for accessing a file
  inside the repository.

  @param repo_path The full or relative path to a backup repository.
  @param info The informations describing the file, for which the path
  should be build.
*/
static void fillPathBufferWithInfo(String repo_path,
                                   const RegularFileInfo *info)
{
  size_t prefix_length = snprintf(NULL, 0, "%i-", info->slot);
  size_t required_capacity =
    prefix_length + snprintf(NULL, 0, "-%zu", info->size);
  required_capacity += FILE_HASH_SIZE * 2;
  required_capacity += 2; /* Reserve some room for the slash and '\0'. */
  required_capacity = sSizeAdd(required_capacity, repo_path.length);
  pathBufferEnsureCapacity(required_capacity);

  memcpy(path_buffer, repo_path.str, repo_path.length);
  path_buffer[repo_path.length] = '/';

  char *prefix_buffer = &path_buffer[repo_path.length + 1];
  sprintf(prefix_buffer, "%i-", info->slot);

  char *hash_buffer = &prefix_buffer[prefix_length];
  for(size_t index = 0; index < FILE_HASH_SIZE; index++)
  {
    sprintf(&hash_buffer[index * 2], "%02x", info->hash[index]);
  }

  char *suffix_buffer = &hash_buffer[FILE_HASH_SIZE * 2];
  sprintf(suffix_buffer, "-%zu", info->size);
}

/** Creates a new RepoWriter from its arguments. Contains the core logic
  behind repoWriterOpenFile() and repoWriterOpenRaw(). */
static RepoWriter *createRepoWriter(const char *repo_path,
                                    const char *repo_tmp_file_path,
                                    const char *source_file_path,
                                    bool raw_mode)
{
  RepoWriter *writer = sMalloc(sizeof *writer);

  writer->repo_path = repo_path;
  writer->repo_tmp_file_path = repo_tmp_file_path;
  writer->source_file_path = source_file_path;
  writer->stream = sFopenWrite(repo_tmp_file_path);
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
  return sPathExists(path_buffer);
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
  fillPathBufferWithInfo(str(repo_path), info);
  FILE *stream = fopen(path_buffer, "rb");
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
  after flushing. The returned RepoWriter will keep a reference to this
  string, so make sure not to modify or free it as long as the writer is in
  use. The file in this path must be inside the repository in order to be
  effective.
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

    die("IO error while writing \"%s\" to \"%s\"",
        source_file_path, repo_path);
  }
}

/** Finalizes the write process represented by the given writer. All its
  data will be written to disk and the temporary file will be renamed to
  its final filename.

  @param writer The writer which should be finalized. This function will
  destroy the writer and free all memory associated with it.
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
    fillPathBufferWithInfo(str(writer.repo_path), writer.rename_to.info);
    sRename(writer.repo_tmp_file_path, path_buffer);
  }

  int dir_descriptor = open(writer.repo_path, O_RDONLY, 0);
  if(dir_descriptor == -1 ||
     fdatasync(dir_descriptor) != 0 ||
     close(dir_descriptor) != 0)
  {
    dieErrno("failed to sync \"%s\" to device", writer.repo_path);
  }
}
