#ifndef NANO_BACKUP_SRC_STR_H
#define NANO_BACKUP_SRC_STR_H

#include <stdbool.h>
#include <stddef.h>

#include "allocator.h"

/** Immutable string slice which doesn't own the memory it points to. */
typedef struct
{
  /** May not be null terminated. Use strGetContent() for interacting with
    C functions. */
  const char *const content;
  const size_t length;
  const bool is_terminated;
} StringView;

extern StringView str(const char *string);
extern StringView strUnterminated(const char *string, size_t length);
extern const char *strGetContent(StringView string, Allocator *a);
extern void strSet(StringView *string, StringView value);
extern bool strEqual(StringView a, StringView b);
extern StringView strCopy(StringView string, Allocator *a);
extern char *strCopyRaw(StringView string, Allocator *a);

extern StringView strStripTrailingSlashes(StringView string);
extern bool strWhitespaceOnly(StringView string);
extern bool strIsDotElement(StringView string);
extern bool strPathContainsDotElements(StringView path);
extern bool strIsParentPath(StringView parent, StringView path);

typedef struct
{
  StringView head;
  StringView tail;
} PathSplit;
extern PathSplit strSplitPath(StringView path);

extern StringView strLegacyCopy(StringView string);
extern StringView strLegacyAppendPath(StringView path,
                                      StringView filename);

#endif
