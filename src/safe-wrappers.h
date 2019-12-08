/** @file
  Declares safe wrapper functions, which handle errors by terminating the
  program with an error message.
*/

#ifndef NANO_BACKUP_SRC_SAFE_WRAPPERS_H
#define NANO_BACKUP_SRC_SAFE_WRAPPERS_H

#include <time.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

extern FileStream *sFopenRead(String path);
extern FileStream *sFopenWrite(String path);
extern void sFread(void *ptr, size_t size, FileStream *stream);
extern void sFwrite(const void *ptr, size_t size, FileStream *stream);
extern bool Fwrite(const void *ptr, size_t size, FileStream *stream);
extern bool Ftodisk(FileStream *stream);
extern bool sFbytesLeft(FileStream *stream);
extern void sFclose(FileStream *stream);
extern String Fdestroy(FileStream *stream);

extern DIR *sOpenDir(String path);
extern struct dirent *sReadDir(DIR *dir, String path);
extern void sCloseDir(DIR *dir, String path);

extern bool sPathExists(String path);
extern struct stat sStat(String path);
extern struct stat sLStat(String path);
extern void sMkdir(String path);
extern void sSymlink(String target, String path);
extern void sRename(String oldpath, String newpath);
extern void sChmod(String path, mode_t mode);
extern void sChown(String path, uid_t user, gid_t group);
extern void sLChown(String path, uid_t user, gid_t group);
extern void sUtime(String path, time_t time);
extern void sRemove(String path);
extern void sRemoveRecursively(String path);

extern char *sGetCwd(void);
extern char *sReadLine(FILE *stream);
extern size_t sStringToSize(String string);
extern time_t sTime(void);
extern int sRand(void);

/** A simple struct, containing the content of the file and its size. */
typedef struct
{
  /** Content of the file. */
  char *content;

  /** The size of the content in bytes. */
  size_t size;
}FileContent;

extern FileContent sGetFilesContent(CR_Region *region, String path);

#endif
