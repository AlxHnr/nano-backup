#include "str.h"

#include <string.h>

#include "CRegion/alloc-growable.h"
#include "CRegion/global-region.h"
#include "CRegion/region.h"

#include "safe-math.h"

/** Wrap the given C string. */
StringView str(const char *string)
{
  return (StringView){
    .content = string,
    .length = strlen(string),
    .is_terminated = true,
  };
}

/** Return a slice containing the given string. */
StringView strUnterminated(const char *string, const size_t length)
{
  return (StringView){
    .content = string,
    .length = length,
    .is_terminated = false,
  };
}

/** Retrieve a null-terminated raw C string from the given string view.

  @param a Allocator which will be used for copying if the given string is
  not null-terminated.

  @return Points either into the given string view or into newly allocated
  memory.
*/
const char *strGetContent(StringView string, Allocator *a)
{
  if(string.is_terminated)
  {
    return string.content;
  }

  char *content = allocate(a, sSizeAdd(string.length, 1));
  memcpy(content, string.content, string.length);
  content[string.length] = '\0';

  return content;
}

/** Creates a copy of the given string.

  @param string The string to copy.

  @return A new string which lifetime will be bound to the entire program.
*/
StringView strLegacyCopy(StringView string)
{
  char *cstring = CR_RegionAllocUnaligned(CR_GetGlobalRegion(),
                                          sSizeAdd(string.length, 1));
  memcpy(cstring, string.content, string.length);
  cstring[string.length] = '\0';

  return (StringView){
    .content = cstring,
    .length = string.length,
    .is_terminated = true,
  };
}

/** Set the content of the given string to the specified value. */
void strSet(StringView *string, StringView value)
{
  memcpy(string, &value, sizeof(StringView));
}

bool strEqual(StringView a, StringView b)
{
  return a.length == b.length &&
    memcmp(a.content, b.content, a.length) == 0;
}

/** Removes trailing slashes from a string.

  @param string A string which may contain trailing slashes.

  @return The same string with a shorter length. It points to the same
  content as the initial string.
*/
StringView strRemoveTrailingSlashes(StringView string)
{
  size_t new_length = string.length;
  while(new_length > 0 && string.content[new_length - 1] == '/')
  {
    new_length--;
  }

  return (StringView){
    .content = string.content,
    .length = new_length,
    .is_terminated = new_length == string.length && string.is_terminated,
  };
}

/** Appends the two given strings with a slash in between. E.g.
  strAppendPath("/etc", "init.d") -> "/etc/init.d".

  @param path The string to which b will be appended.
  @param filename The string which will be appended to a.

  @return A new string which lifetime will be bound to the entire program.
*/
StringView strLegacyAppendPath(StringView path, StringView filename)
{
  const size_t buffer_size =
    sSizeAdd(sSizeAdd(path.length, filename.length), 2);
  const size_t path_length = buffer_size - 1;

  char *cstring =
    CR_RegionAllocUnaligned(CR_GetGlobalRegion(), buffer_size);

  memcpy(cstring, path.content, path.length);
  cstring[path.length] = '/';
  memcpy(&cstring[path.length + 1], filename.content, filename.length);
  cstring[path_length] = '\0';

  return (StringView){
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
PathSplit strSplitPath(StringView path)
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

  return (PathSplit){
    (StringView){
      .content = path.content,
      .length = last_slash > 0 ? last_slash - 1 : 0,
      .is_terminated = false,
    },
    (StringView){
      .content = &path.content[last_slash],
      .length = path.length - last_slash,
      .is_terminated = path.is_terminated,
    },
  };
}

/** Returns true if the given string is empty, or contains only
  whitespaces. */
bool strWhitespaceOnly(StringView string)
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
bool strIsDotElement(StringView string)
{
  return (string.length == 1 && string.content[0] == '.') ||
    (string.length == 2 && string.content[0] == '.' &&
     string.content[1] == '.');
}

/** Checks if a path contains the elements "." or "..". E.g.
  "/home/foo/../test.txt". Multiple slashes will be treated like in
  strSplitPath(). E.g. "/home/foo//." will return false.

  @param path The path to check.

  @return True if the given path contains the elements "." or "..".
*/
bool strPathContainsDotElements(StringView path)
{
  PathSplit split = strSplitPath(path);

  return strIsDotElement(split.tail) ||
    (split.head.length > 0 && strPathContainsDotElements(split.head));
}

/** Returns true if the given path starts with the specified parent. E.g.
  strIsParentPath("/etc", "/etc/portage") .

  @param parent The parent path which should not end with a slash.
  @param path The full path which could start with the parent.

  @return True if path starts with parent.
*/
bool strIsParentPath(StringView parent, StringView path)
{
  return parent.length < strRemoveTrailingSlashes(path).length &&
    path.content[parent.length] == '/' &&
    memcmp(path.content, parent.content, parent.length) == 0;
}
