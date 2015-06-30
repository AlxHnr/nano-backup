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

  @param string A null-terminated string that should be wrapped.

  @return A string struct.
*/
String str(const char *string)
{
  return (String){ .str = string, .length = strlen(string) };
}

/** Copies the content of a String. It uses the internal memory pool for
  allocations, so use this function only for strings which live as long as
  the entire program.

  @param string A String, which data should be copied.

  @return A String, which content should not be freed by the caller. The
  buffer to which the returned string points to will be null-terminated.
*/
String strCopy(String string)
{
  char *new_string = mpAlloc(sSizeAdd(string.length, 1));
  memcpy(new_string, string.str, string.length);
  new_string[string.length] = '\0';

  return (String){ .str = new_string, .length = string.length };
}

/** Removes trailing characters from a string.

  @param string The string to be trimmed.
  @param c The character that should be removed.

  @return The same string with a shorter length. It points into the given
  string so make sure not to free or modify it, unless the returned string
  is not used anymore.
*/
String strRemoveTrailing(String string, char c)
{
  size_t new_length = string.length;
  while(new_length > 0 && string.str[new_length - 1] == c) new_length--;

  return (String){ .str = string.str, .length = new_length };
}

/** Appends two paths and inserts a slash in between. It uses the internal
  memory pool for allocations, so use this function only for strings which
  live as long as the entire program.

  @param path A file path.
  @param filename A filename.

  @return A new String that should not be freed by the caller. The buffer
  to which the returned string points to will be null-terminated.
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

/** Compares two strings.

  @param a The first String to compare.
  @param b The second String to compare.

  @return True if both strings have the same length, and all bytes up to
  'length' are the same. Otherwise false.
*/
bool strCompare(String a, String b)
{
  return a.length == b.length && memcmp(a.str, b.str, a.length) == 0;
}

/** Return true if the given string is empty, or contains only whitespaces.
*/
bool strWhitespaceOnly(String string)
{
  for(size_t index = 0; index < string.length; index++)
  {
    if(string.str[index] != ' ' && string.str[index] != '\t')
    {
      return false;
    }
  }

  return true;
}
