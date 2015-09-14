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

/** @file
  Declares functions for safe writing of files into backup repositories.
  The idea behind this is to create a temporary file, sync it to the device
  and only then rename it to its final filename. This prevents existing
  files from being overwritten by partial/broken files, in case the program
  crashes.
*/

#ifndef _NANO_BACKUP_SAFE_WRITE_H_
#define _NANO_BACKUP_SAFE_WRITE_H_

#include <stddef.h>

/** An opaque struct, which allows safe creation of files inside
  directories. */
typedef struct SafeWriteHandle SafeWriteHandle;

extern SafeWriteHandle *openSafeWriteHandle(const char *dir_path,
                                            const char *filename,
                                            const char *real_file_path);
extern void writeSafeWriteHandle(const void *data, size_t size,
                                 SafeWriteHandle *handle);
extern void closeSafeWriteHandle(SafeWriteHandle *handle);

#endif
