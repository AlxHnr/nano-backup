/** @file
  Declares various functions for string manipulation.
*/

#ifndef NANO_BACKUP_STR_H
#define NANO_BACKUP_STR_H

#include <stddef.h>
#include <stdbool.h>

/** A string type which doesn't own the memory it points to. It can be used
  for cheap string slicing.
*/
typedef struct
{
  /** A pointer to the beginning of the string. It may not be null
    terminated. Use cStr() for passing Strings to C functions. */
  const char *const content;

  /** The length of the string. */
  const size_t length;

  /** True if the content is null terminated. */
  const bool is_terminated;
}String;

/** A struct representing a string splitting. */
typedef struct
{
  String head; /**< The part before the split. */
  String tail; /**< The part after the split. */
}StringSplit;

extern String strWrap(const char *string);
extern String strSlice(const char *string, size_t length);
extern String strCopy(String string);
extern void strSet(String *string, String value);
extern bool strEqual(String a, String b);
extern const char *cStr(String string, char **buffer);

extern String strRemoveTrailingSlashes(String string);
extern String strAppendPath(String path, String filename);
extern StringSplit strSplitPath(String path);
extern bool strWhitespaceOnly(String string);
extern bool strIsDotElement(String string);
extern bool strPathContainsDotElements(String path);
extern bool strIsParentPath(String parent, String path);

#endif
