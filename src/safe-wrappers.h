#ifndef NANO_BACKUP_SRC_SAFE_WRAPPERS_H
#define NANO_BACKUP_SRC_SAFE_WRAPPERS_H

#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <dirent.h>
#include <sys/stat.h>

#include "CRegion/region.h"

#include "str.h"

extern void *sMalloc(size_t size);
extern void *sRealloc(void *ptr, size_t size);

extern void sAtexit(void (*function)(void));

/** An opaque wrapper around FILE, which stores additional informations for
  printing better error messages. */
typedef struct FileStream FileStream;

extern FileStream *sFopenRead(StringView path);
extern FileStream *sFopenWrite(StringView path);
extern void sFread(void *ptr, size_t size, FileStream *stream);
extern void sFwrite(const void *ptr, size_t size, FileStream *stream);
extern bool fWrite(const void *ptr, size_t size, FileStream *stream);
extern bool fTodisk(FileStream *stream);
extern bool sFbytesLeft(FileStream *stream);
extern void sFclose(FileStream *stream);
extern void fDestroy(FileStream *stream);

typedef struct DirIterator DirIterator;
extern DirIterator *sDirOpen(StringView path);
extern StringView sDirGetNext(DirIterator *dir);
extern void sDirClose(DirIterator *dir);

extern bool sPathExists(StringView path);
extern struct stat sStat(StringView path);
extern struct stat sLStat(StringView path);
extern void sMkdir(StringView path);
extern void sSymlink(StringView target, StringView path);
extern void sRename(StringView oldpath, StringView newpath);
extern void sChmod(StringView path, mode_t mode);
extern void sChown(StringView path, uid_t user, gid_t group);
extern void sLChown(StringView path, uid_t user, gid_t group);
extern void sUtime(StringView path, time_t time);
extern void sRemove(StringView path);
extern void sRemoveRecursively(StringView path);

typedef bool ShouldRemoveCallback(StringView path,
                                  const struct stat *stats,
                                  void *user_data);
extern void sRemoveRecursivelyIf(StringView path,
                                 ShouldRemoveCallback should_remove,
                                 void *user_data);

extern char *sGetCwd(void);
extern char *sReadLine(FILE *stream);
extern bool sIsTTY(FILE *stream);
extern size_t sStringToSize(StringView string);
extern time_t sTime(void);
extern int sRand(void);

typedef struct
{
  char *content;
  size_t size;
} FileContent;

extern FileContent sGetFilesContent(CR_Region *region, StringView path);

extern const regex_t *rpCompile(StringView expression,
                                StringView file_name, size_t line_nr);

#endif
