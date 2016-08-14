/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

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
extern void sRemove(const char *path);

extern char *sReadLine(FILE *stream);
extern time_t sTime(void);

/** A simple struct, containing the content of the file and its size. */
typedef struct
{
  /** The content of the file. It will point to NULL if the size of the
    file equals to zero.*/
  char *content;

  /** The size of the content in bytes. */
  size_t size;
}FileContent;

extern FileContent sGetFilesContent(const char *path);

#endif
