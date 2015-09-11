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
  Implements functions for safe writing of files into backup repositories.
*/

#include "safe-write.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safe-wrappers.h"
#include "error-handling.h"

struct SafeWriteHandle
{
  /** The full or relative path to the directory to which the current
    handle belongs. */
  const char *dir_path;

  /** The path to the temporary file. */
  char *tmp_file_path;

  /** The stream to write to the temporary file. */
  FileStream *tmp_file_stream;

  /** The final filepath, to which the temporary file gets renamed. */
  char *dest_path;

  /** The name or path of the file represented by this handle. It differs
    from @link dest_path @endlink and contains a human readable name/path
    for useful error printing. */
  const char *real_file_path;
};

/** Frees the memory associated with the given handler. This function does
  not finalize the write process, nor does it close its file handles.

  @param handle The handle which should be freed.
*/
static void freeSafeWriteHandle(SafeWriteHandle *handle)
{
  free(handle->tmp_file_path);
  free(handle->dest_path);
  free(handle);
}

/** Appends a slash and the given filename to the specified path. It
  differs from strAppendPath() by not allocating the string buffer inside
  the programs internal memory pool.

  @param path The path, to which the filename should be appended.
  @param filename The filename, which should be appended to the path.

  @return A new path, which must be freed by the caller using free().
*/
static char *appendPath(const char *path, const char *filename)
{
  size_t path_length = strlen(path);
  size_t filename_length = strlen(filename);

  size_t required_capacity =
    sSizeAdd(path_length, sSizeAdd(filename_length, 2));

  char *new_path = sMalloc(required_capacity);
  memcpy(new_path, path, path_length);
  memcpy(&new_path[path_length + 1], filename, filename_length);
  new_path[path_length] = '/';
  new_path[required_capacity - 1] = '\0';

  return new_path;
}

/** Creates a new write handle for safe creation of files. The caller must
  ensure that no open handle exists for the specified directory, or it will
  lead to undefined behaviour. This function will create a file named
  "tmp-file" inside the directory, which may get removed or overwritten at
  any time while the handle is open.

  @param dir_path The full or relative path of the directory inside which
  the file should be created. The returned handle will keep a reference to
  this string until it gets closed.
  @param filename The name of the file inside the directory, which should
  be the destination of all writes trough this handle.
  @param real_file_path A human readable path or name of the file that the
  returned handle represents. While the filename inside the specified
  directory can be a number or a hash, this string will be printed to the
  user on errors. The returned handle will keep a reference to this string
  until it gets closed.

  @return A new write handle which must be closed by the caller using
  closeSafeWriteHandle().
*/
SafeWriteHandle *openSafeWriteHandle(const char *dir_path,
                                     const char *filename,
                                     const char *real_file_path)
{
  SafeWriteHandle *handle = sMalloc(sizeof *handle);

  handle->dir_path        = dir_path;
  handle->tmp_file_path   = appendPath(dir_path, "tmp-file");
  handle->tmp_file_stream = sFopenWrite(handle->tmp_file_path);
  handle->dest_path       = appendPath(dir_path, filename);
  handle->real_file_path  = real_file_path;

  return handle;
}

/** Writes data trough a SafeWriteHandle and terminates the program on
  failure.

  @param handle The handle, which should be used to writing to the file.
  @param data The data, which should be written.
  @param size The size of the specified data in bytes.
*/
void writeSafeWriteHandle(SafeWriteHandle *handle,
                          const void *data, size_t size)
{
  if(Fwrite(data, size, handle->tmp_file_stream) == false)
  {
    const char *dir_path = handle->dir_path;
    const char *real_file_path = handle->real_file_path;

    Fdestroy(handle->tmp_file_stream);
    freeSafeWriteHandle(handle);

    die("IO error while writing \"%s\" to \"%s\"",
        real_file_path, dir_path);
  }
}

/** Finalizes the write process represented by the given handle. All its
  data will be written to disk and the temporary file will be renamed to
  its final filename.

  @param handle The handle which write process should be finished. This
  function will destroy the handle and free all memory associated with it.
*/
void closeSafeWriteHandle(SafeWriteHandle *handle)
{
  const char *dir_path = handle->dir_path;
  const char *real_file_path = handle->real_file_path;

  if(Ftodisk(handle->tmp_file_stream) == false)
  {
    Fdestroy(handle->tmp_file_stream);
    freeSafeWriteHandle(handle);

    dieErrno("failed to flush/sync \"%s\" to \"%s\"",
             real_file_path, dir_path);
  }

  sFclose(handle->tmp_file_stream);
  sRename(handle->tmp_file_path, handle->dest_path);
  freeSafeWriteHandle(handle);

  int dir_descriptor = open(dir_path, O_WRONLY, 0);
  if(dir_descriptor == -1 ||
     fdatasync(dir_descriptor) != 0 ||
     close(dir_descriptor) != 0)
  {
    dieErrno("failed to sync \"%s\" to device", dir_path);
  }
}
