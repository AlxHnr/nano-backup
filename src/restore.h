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
  Declares functions for restoring files.
*/

#ifndef NANO_BACKUP_RESTORE_H
#define NANO_BACKUP_RESTORE_H

#include "metadata.h"

extern void initiateRestore(Metadata *metadata, size_t id,
                            const char *path);

extern void restoreFile(const char *path,
                        const RegularFileInfo *info,
                        const char *repo_path);
extern void finishRestore(Metadata *metadata, size_t id,
                          const char *repo_path);

#endif
