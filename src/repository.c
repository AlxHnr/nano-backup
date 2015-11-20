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

/** @file
  Implements various helper functions to handle repositories.
*/

#include "repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safe-wrappers.h"

/** A struct for safely writing files into backup repositories. */
struct RepoWriter
{
  /** The path to the repository. */
  const char *repo_path;

  /** The path to the source file in the filesystem, which gets written
    to the repository trough this writer. This is required for printing
    useful error messages. */
  const char *source_file_path;

  /** The FileStream wrapped by this struct. */
  FileStream *stream;

  /** A reference to the informations describing the source file. */
  const RegularFileInfo *info;
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
  size_t required_capacity =
    snprintf(NULL, 0, ":%zu:%i", info->size, info->slot);
  required_capacity += SHA_DIGEST_LENGTH * 2;
  required_capacity += 2; /* Reserve some room for the slash and '\0'. */
  required_capacity = sSizeAdd(required_capacity, repo_path.length);
  pathBufferEnsureCapacity(required_capacity);

  memcpy(path_buffer, repo_path.str, repo_path.length);
  path_buffer[repo_path.length] = '/';

  char *hash_buffer = &path_buffer[repo_path.length + 1];
  for(size_t index = 0; index < SHA_DIGEST_LENGTH; index++)
  {
    sprintf(&hash_buffer[index * 2], "%02x", info->hash[index]);
  }

  char *suffix_buffer = &hash_buffer[SHA_DIGEST_LENGTH * 2];
  sprintf(suffix_buffer, ":%zu:%i", info->size, info->slot);
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
  repository.

  @return A new RepoWriter, which must be closed by the caller using
  repoWriterClose().
*/
RepoWriter *repoWriterOpenFile(const char *repo_path,
                               const char *repo_tmp_file_path,
                               const char *source_file_path,
                               const RegularFileInfo *info)
{
  RepoWriter *writer = sMalloc(sizeof *writer);

  writer->repo_path = repo_path;
  writer->source_file_path = source_file_path;
  writer->stream = sFopenWrite(repo_tmp_file_path);
  writer->info = info;

  return writer;
}
