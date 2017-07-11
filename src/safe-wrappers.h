/** @file
  Declares safe wrapper functions, which handle errors by terminating the
  program with an error message.
*/

#ifndef NANO_BACKUP_SAFE_WRAPPERS_H
#define NANO_BACKUP_SAFE_WRAPPERS_H

#include <time.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <dirent.h>
#include <sys/stat.h>

extern void *sMalloc(size_t size);
extern void *sRealloc(void *ptr, size_t size);

extern size_t sSizeAdd(size_t a, size_t b);
extern size_t sSizeMul(size_t a, size_t b);
extern uint64_t sUint64Add(uint64_t a, uint64_t b);

extern void sAtexit(void (*function)(void));

/** An opaque wrapper around FILE, which stores additional informations for
  printing better error messages. */
typedef struct FileStream FileStream;

extern FileStream *sFopenRead(const char *path);
extern FileStream *sFopenWrite(const char *path);
extern void sFread(void *ptr, size_t size, FileStream *stream);
extern void sFwrite(const void *ptr, size_t size, FileStream *stream);
extern bool Fwrite(const void *ptr, size_t size, FileStream *stream);
extern bool Ftodisk(FileStream *stream);
extern bool sFbytesLeft(FileStream *stream);
extern void sFclose(FileStream *stream);
extern const char *Fdestroy(FileStream *stream);

extern DIR *sOpenDir(const char *path);
extern struct dirent *sReadDir(DIR *dir, const char *path);
extern void sCloseDir(DIR *dir, const char *path);

extern bool sPathExists(const char *path);
extern struct stat sStat(const char *path);
extern struct stat sLStat(const char *path);
extern void sMkdir(const char *path);
extern void sSymlink(const char *target, const char *path);
extern void sRename(const char *oldpath, const char *newpath);
extern void sChmod(const char *path, mode_t mode);
extern void sChown(const char *path, uid_t user, gid_t group);
extern void sLChown(const char *path, uid_t user, gid_t group);
extern void sUtime(const char *path, time_t time);
extern void sRemove(const char *path);
extern void sRemoveRecursively(const char *path);

extern char *sGetCwd(void);
extern char *sReadLine(FILE *stream);
extern size_t sStringToSize(const char *string);
extern time_t sTime(void);
extern int sRand(void);

/** A simple struct, containing the content of the file and its size. */
typedef struct
{
  /** The content of the file. It will point to NULL if the size of the
    file equals to zero. */
  char *content;

  /** The size of the content in bytes. */
  size_t size;
}FileContent;

extern FileContent sGetFilesContent(const char *path);

#endif
