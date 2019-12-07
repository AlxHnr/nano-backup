/** @file
  Implements functions for string manipulation.
*/

#include "str.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "CRegion/global-region.h"
#include "CRegion/region.h"

#include "safe-math.h"

/** Wraps the given C string into a String.

  @param string The C string to wrap.

  @return A String which will point to the given C string.
*/
String strWrap(const char *string)
{
  return (String)
  {
    .content = string,
    .length = strlen(string),
    .is_terminated = true,
  };
}

/** Returns a slice of the given string.

  @param string The string to wrap into a String slice.
  @param length The length of the wrapped string.

  @return A String which will point to the given C string.
*/
String strSlice(const char *string, size_t length)
{
  return (String)
  {
    .content = string,
    .length = length,
    .is_terminated = false,
  };
}

/** Creates a copy of the given string.

  @param string The string to copy.

  @return A new string which lifetime will be bound to the given region.
*/
String strCopy(String string)
{
  char *cstring = CR_RegionAllocUnaligned(CR_GetGlobalRegion(),
                                          sSizeAdd(string.length, 1));
  memcpy(cstring, string.content, string.length);
  cstring[string.length] = '\0';

  return (String)
  {
    .content = cstring,
    .length = string.length,
    .is_terminated = true,
  };
}

/** Sets the content of the given string to the specified value.

  @param string The string to update.
  @param value The value the string should be set to.
*/
void strSet(String *string, String value)
{
  memcpy(string, &value, sizeof(String));
}

/** Returns true if the given strings have the same length and content. */
bool strEqual(String a, String b)
{
  return a.length == b.length &&
    memcmp(a.content, b.content, a.length) == 0;
}

/** Returns a null terminated version of the given string.

  @param string The string to terminate.
  @param buffer A buffer to use in case the given string is not terminated
  and needs to be copied. This pointer to a buffer will be updated on
  allocations.

  @return A pointer to either the given strings content, or to the given
  buffer.
*/
const char *cStr(String string, char **buffer)
{
  if(string.is_terminated)
  {
    return string.content;
  }
  else
  {
    *buffer = CR_EnsureCapacity(*buffer, sSizeAdd(string.length, 1));
    memcpy(*buffer, string.content, string.length);
    (*buffer)[string.length] = '\0';

    return *buffer;
  }
}

/** Removes trailing slashes from a string.

  @param string A string which may contain trailing slashes.

  @return The same string with a shorter length. It points to the same
  content as the initial string.
*/
String strRemoveTrailingSlashes(String string)
{
  size_t new_length = string.length;
  while(new_length > 0 && string.content[new_length - 1] == '/')
  {
    new_length--;
  }

  return (String)
  {
    .content = string.content,
    .length = new_length,
    .is_terminated =
      new_length == string.length && string.is_terminated,
  };
}

/** Appends the two given strings with a slash in between. E.g.
  strAppendPath("/etc", "init.d") -> "/etc/init.d".

  @param a The string to which b will be appended.
  @param b The string which will be appended to a.

  @return A new string which lifetime will be bound to the given region.
*/
String strAppendPath(String a, String b)
{
  const size_t buffer_size = sSizeAdd(sSizeAdd(a.length, b.length), 2);
  const size_t path_length = buffer_size - 1;

  char *cstring = CR_RegionAllocUnaligned(CR_GetGlobalRegion(), buffer_size);

  memcpy(cstring, a.content, a.length);
  cstring[a.length] = '/';
  memcpy(&cstring[a.length + 1], b.content, b.length);
  cstring[path_length] = '\0';

  return (String)
  {
    .content = cstring,
    .length = path_length,
    .is_terminated = true,
  };
}

/** Splits the given path at the last slash it contains. If the last slash
  is preceded by more slashes, then the first slash of them will be seen as
  the split point. E.g. "/home/foo///bar" -> [ "/home/foo", "//bar" ].

  @param path The path that should be split. The returned splitting will
  keep a reference into this string.

  @return Two strings that share memory with the given path. If the path
  doesn't contain a slash, the head of the returned splitting will be empty
  and the tail will contain the entire string. If the path ends with a
  slash, the head will contain the entire string and the tail will be
  empty.
*/
StringSplit strSplitPath(String path)
{
  size_t last_slash = path.length;
  while(last_slash > 0 && path.content[last_slash - 1] != '/')
  {
    last_slash--;
  }

  /* If the last slash is preceded by other slashes, get its position
     instead. */
  while(last_slash > 1 && path.content[last_slash - 2] == '/')
  {
    last_slash--;
  }

  return (StringSplit)
  {
    (String)
    {
      .content = path.content,
      .length = last_slash > 0 ? last_slash - 1 : 0,
      .is_terminated = false,
    },
    (String)
    {
      .content       = &path.content[last_slash],
      .length        = path.length - last_slash,
      .is_terminated = path.is_terminated,
    }
  };
}

/** Returns true if the given string is empty, or contains only
  whitespaces. */
bool strWhitespaceOnly(String string)
{
  for(size_t index = 0; index < string.length; index++)
  {
    if(string.content[index] != ' ' && string.content[index] != '\t')
    {
      return false;
    }
  }

  return true;
}

/** Returns true if the given string is "." or "..". */
bool strIsDotElement(String string)
{
  return
    (string.length == 1 && string.content[0] == '.') ||
    (string.length == 2 &&
     string.content[0] == '.' &&
     string.content[1] == '.');
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
    path.content[parent.length] == '/' &&
    memcmp(path.content, parent.content, parent.length) == 0;
}
