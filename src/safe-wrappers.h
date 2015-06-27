/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

/**
  @file safe-wrappers.h Declares safe wrapper functions, which handle
  errors by terminating the program with an error message.
*/

#ifndef _NANO_BACKUP_SAFE_WRAPPERS_H_
#define _NANO_BACKUP_SAFE_WRAPPERS_H_

#include <stdio.h>
#include <sys/stat.h>

extern void *sMalloc(size_t size);
extern void *sRealloc(void *ptr, size_t size);

extern size_t sSizeAdd(size_t a, size_t b);
extern size_t sSizeMul(size_t a, size_t b);

extern FILE *sFopenRead(const char *path);
extern FILE *sFopenWrite(const char *path);
extern void sFclose(FILE *stream, const char *path);

extern void sFread(void *ptr, size_t size, FILE *stream, const char *path);

extern struct stat sStat(const char *path);
extern struct stat sLStat(const char *path);

/** A simple struct, representing the content of the file and its size. */
typedef struct
{
  void *data; /**< The content of a file. */
  size_t size; /**< The length of the content. */
}FileContent;

extern FileContent sGetFilesContent(const char *path);

#endif
