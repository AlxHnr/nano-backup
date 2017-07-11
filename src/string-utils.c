/** @file
  Implements various helper functions for manipulating strings.
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

/** Returns true if the given string is "." or "..". */
bool strIsDotElement(String string)
{
  return
    (string.length == 1 && string.str[0] == '.') ||
    (string.length == 2 && string.str[0] == '.' && string.str[1] == '.');
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
    strIsDotElement(split.tail) ||
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
