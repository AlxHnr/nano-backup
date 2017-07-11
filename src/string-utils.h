/** @file
  Declares various helper functions for manipulating strings.
*/

#ifndef NANO_BACKUP_STRING_UTILS_H
#define NANO_BACKUP_STRING_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** A simple struct associating a string with its length. This struct
  doesn't own the memory it points to and can be used for efficient string
  slicing.
*/
typedef struct
{
  /** A pointer to the beginning of the string. It doesn't need to be null
    terminated, but be careful when passing it to C library functions. */
  const char *const str;

  /** The length of the string. It can be shorter than the actual string
    stored in "str", but be careful when passing it to C library
    functions. */
  const size_t length;
}String;

/** A struct representing a string splitting. */
typedef struct
{
  String head; /**< The part before the split. */
  String tail; /**< The part after the split. */
}StringSplit;

extern String str(const char *string);
extern String strCopy(String string);

extern bool strCompare(String a, String b);
extern bool strWhitespaceOnly(String string);

extern String strRemoveTrailingSlashes(String string);
extern String strAppendPath(String path, String filename);
extern StringSplit strSplitPath(String path);
extern bool strIsDotElement(String string);
extern bool strPathContainsDotElements(String path);
extern bool strIsParentPath(String parent, String path);

#endif
