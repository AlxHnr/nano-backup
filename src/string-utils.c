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
  Implements various helper functions for manipulating strings.
*/

#include "string-utils.h"

#include <string.h>

#include "memory-pool.h"
#include "safe-wrappers.h"

/** A magic prime number for calculating the murmur2 hash of a String. */
#define MURMUR2_MAGIC_NUMBER 15486883

/** A magic seed for the murmur2 function. */
#define MURMUR2_MAGIG_SEED 179425849

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

/** Calculates the 32-bit hash of the given string.

  @param string The string that should be hashed.

  @return An unsigned 32 bit hash value.
*/
uint32_t strHash(String string)
{
  /* This function implements the murmur2 hashing algorithm. */
  const uint8_t *data = (const uint8_t *)string.str;
  uint32_t hash = string.length * MURMUR2_MAGIG_SEED;
  size_t bytes_left = string.length;

  while(bytes_left >= 4)
  {
    uint32_t key = *(const uint32_t *)data;

    key *= MURMUR2_MAGIC_NUMBER;
    key ^= key >> 24;
    key *= MURMUR2_MAGIC_NUMBER;

    hash *= MURMUR2_MAGIC_NUMBER;
    hash ^= key;

    data += 4;
    bytes_left -= 4;
  }

  switch(bytes_left)
  {
    case 3: hash ^= data[2] << 16;
    case 2: hash ^= data[1] << 8;
    case 1: hash ^= data[0];
            hash *= MURMUR2_MAGIC_NUMBER;
  }

  hash ^= hash >> 13;
  hash *= MURMUR2_MAGIC_NUMBER;
  hash ^= hash >> 15;

  return hash;
}

/** Removes trailing slashes from a string.

  @param string A string which may contain trailing slashes.

  @return The same string with a shorter length. It points to the same
  memory as the initial string, so make sure not to free or modify it
  unless the returned string is not used anymore.
*/
String strRemoveTrailingSlashes(String string)
{
  size_t new_length = string.length;
  while(new_length > 0 && string.str[new_length - 1] == '/') new_length--;

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

/** Splits the given path at the last slash it contains. If the last slash
  is preceded by more slashes, then the first slash of them will be seen as
  the split point. E.g. "/home/foo///bar" -> [ "/home/foo", "//bar" ].

  @param path The path that should be split. The returned splitting will
  keep a reference into this string, so make sure not to modify or free it
  unless the returned split is not used anymore.

  @return Two strings that share memory with the given path. If the path
  doesn't contain a slash, the head of the returned splitting will be empty
  and the tail will contain the entire string. If the path ends with a
  slash, the head will contain the entire string and the tail will be
  empty.
*/
StringSplit strSplitPath(String path)
{
  size_t last_slash = path.length;
  while(last_slash > 0 && path.str[last_slash - 1] != '/') last_slash--;

  /* If the last slash is preceded by other slashes, get its position
     instead. */
  while(last_slash > 1 && path.str[last_slash - 2] == '/') last_slash--;

  return (StringSplit)
  {
    (String)
    {
      .str = path.str,
      .length = last_slash > 0 ? last_slash - 1 : 0
    },
    (String)
    {
      .str = &path.str[last_slash],
      .length = path.length - last_slash
    }
  };
}

/** Checks if a path contains the elements "." or "..". E.g.
  "/home/foo/../test.txt". Multiple slashes will be treated like in
  strSplitPath(). E.g. "/home/foo//." will return false.

  @param path The path to check.

  @return True if the given path contains the elements "." or "..".
*/
bool strPathContainsDotElements(String path)
{
  StringSplit split = strSplitPath(path);

  return
    (split.tail.length == 1 &&
     split.tail.str[0] == '.') ||
    (split.tail.length == 2 &&
     split.tail.str[0] == '.' &&
     split.tail.str[1] == '.') ||
    (split.head.length > 0 &&
     strPathContainsDotElements(split.head));
}

/** Returns true if the given path starts with the specified parent. E.g.
  strIsParentPath("/etc", "/etc/portage") == true.

  @param parent The parent path which should not end with a slash.
  @param path The full path which could start with the parent.

  @return True if path starts with parent.
*/
bool strIsParentPath(String parent, String path)
{
  return parent.length < strRemoveTrailingSlashes(path).length &&
    path.str[parent.length] == '/' &&
    memcmp(path.str, parent.str, parent.length) == 0;
}
