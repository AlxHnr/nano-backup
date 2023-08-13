#ifndef NANO_BACKUP_SRC_STR_H
#define NANO_BACKUP_SRC_STR_H

#include <stdbool.h>
#include <stddef.h>

/** Immutable string slice which doesn't own the memory it points to. */
typedef struct
{
  /** May not be null terminated. Use strRaw() for interacting with C
    functions. */
  const char *const content;
  const size_t length;
  const bool is_terminated;
} String;

extern String strWrap(const char *string);
extern String strWrapLength(const char *string, size_t length);
extern const char *strRaw(String string, char **buffer);

extern String strCopy(String string);
extern void strSet(String *string, String value);
extern bool strEqual(String a, String b);

extern String strRemoveTrailingSlashes(String string);
extern String strAppendPath(String path, String filename);
extern bool strWhitespaceOnly(String string);
extern bool strIsDotElement(String string);
extern bool strPathContainsDotElements(String path);
extern bool strIsParentPath(String parent, String path);

typedef struct
{
  String head;
  String tail;
} PathSplit;
extern PathSplit strSplitPath(String path);

#endif
