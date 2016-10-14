/*
  Copyright (c) 2016 Alexander Heinrich

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
  Implements functions to build reusable paths based on Buffer.
*/

#include "path-builder.h"

#include <string.h>

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
