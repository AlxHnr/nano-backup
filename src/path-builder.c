/** @file
  Implements functions to build reusable paths based on Buffer.
*/

#include "path-builder.h"

#include <string.h>

#include "CRegion/alloc-growable.h"

#include "safe-math.h"
#include "safe-wrappers.h"

/** Copies the given path into the specified Buffer.

  @param buffer_ptr Buffer for storing the given path. This buffer must
  have been created by CR_RegionAllocGrowable() or must point to NULL
  otherwise. If it points to NULL, a new buffer will be allocated by
  CR_EnsureCapacity(), to which the given buffer pointer will be point to.
  @param path Will be copied to the given buffer.

  @return The length of the given path.
*/
size_t pathBuilderSet(char **buffer_ptr, const char *path)
{
  size_t length = strlen(path);

  *buffer_ptr = CR_EnsureCapacity(*buffer_ptr, sSizeAdd(length, 1));
  memcpy(*buffer_ptr, path, length);
  (*buffer_ptr)[length] = '\0';

  return length;
}

/** Appends the given path to a Buffer.

  @param buffer_ptr Contains a path to which the given path should be
  appended to. This buffer must have been created by
  CR_RegionAllocGrowable() or must point to NULL otherwise. If it points to
  NULL, a new buffer will be allocated by CR_EnsureCapacity(), to which the
  given buffer pointer will be point to.
  @param length The length of the path in the buffer. This is the position
  to which "/path" will be appended.
  @param path Path to append to the given buffer.

  @return The size of the new path in buffer.
*/
size_t pathBuilderAppend(char **buffer_ptr, size_t length, const char *path)
{
  size_t path_length = strlen(path);
  size_t required_capacity = sSizeAdd(sSizeAdd(length, 2), path_length);

  *buffer_ptr = CR_EnsureCapacity(*buffer_ptr, required_capacity);

  (*buffer_ptr)[length] = '/';
  (*buffer_ptr)[required_capacity - 1] = '\0';
  memcpy(&(*buffer_ptr)[length + 1], path, path_length);

  return required_capacity - 1;
}
