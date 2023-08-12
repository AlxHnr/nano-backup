#ifndef NANO_BACKUP_SRC_STR_H
#define NANO_BACKUP_SRC_STR_H

#include <stdbool.h>
#include <stddef.h>

/** A string slice type which doesn't own the memory it points to. */
typedef struct
{
  /** A pointer to the beginning of the string. It may not be null
    terminated. Use cStr() for passing Strings to C functions. */
  const char *const content;
  const size_t length;
  const bool is_terminated;
} String;

typedef struct
{
  String head;
  String tail;
} StringSplit;

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
