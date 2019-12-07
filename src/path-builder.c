/** @file
  Implements functions to build reusable paths based on Buffer.
*/

#include "path-builder.h"

#include <string.h>

#include "safe-math.h"
#include "safe-wrappers.h"

/** Copies the given path into the specified Buffer.

  @return The length of the given path.
*/
size_t pathBuilderSet(Buffer **buffer, const char *path)
{
  size_t length = strlen(path);

  bufferEnsureCapacity(buffer, sSizeAdd(length, 1));
  memcpy((*buffer)->data, path, length);
  (*buffer)->data[length] = '\0';

  return length;
}

/** Appends the given path to a Buffer.

  @param buffer The buffer containing the path.
  @param length The length of the path in the buffer. This is the position
  to which "/path" will be appended.
  @param path The path to append.

  @return The size of the new path in buffer.
*/
size_t pathBuilderAppend(Buffer **buffer, size_t length, const char *path)
{
  size_t path_length = strlen(path);
  size_t required_capacity = sSizeAdd(sSizeAdd(length, 2), path_length);

  bufferEnsureCapacity(buffer, required_capacity);

  (*buffer)->data[length] = '/';
  (*buffer)->data[required_capacity - 1] = '\0';
  memcpy(&(*buffer)->data[length + 1], path, path_length);

  return required_capacity - 1;
}
