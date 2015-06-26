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

/**
  @file string-utils.c Implements various helper functions for manipulating
  strings.
*/

#include "string-utils.h"

#include <string.h>

#include "memory-pool.h"
#include "safe-wrappers.h"

/** Wraps the given string in a String struct. It doesn't copy the string.

  @param string The string that should be wrapped.

  @return A string struct.
*/
String str(const char *string)
{
  return (String){ .str = string, .length = strlen(string) };
}

/** Appends two paths and inserts a slash in between. It uses the internal
  memory pool for allocations, so use this function only for strings which
  live as long as the entire program.

  @param path A file path.
  @param filename A filename.

  @return A new String that should not be freed by the caller.
*/
String strAppendPath(String path, String filename)
{
  size_t new_length = sSizeAdd(sSizeAdd(path.length, filename.length), 1);
  char *new_path = mpAlloc(sSizeAdd(new_length, 1));

  new_path[path.length] = '/';
  new_path[new_length] = '\0';

  memcpy(new_path, path.str, path.length);
  memcpy(&new_path[path.length + 1], filename.str, filename.length);

  return (String){ .str = new_path, .length = new_length };
}
