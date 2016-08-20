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
  Various helper functions for doing backup related things.
*/

#ifndef NANO_BACKUP_BACKUP_HELPERS_H
#define NANO_BACKUP_BACKUP_HELPERS_H

#include <sys/stat.h>

#include "buffer.h"
#include "metadata.h"

extern const char *readSymlink(const char *path, struct stat stats,
                               Buffer **buffer_ptr);
extern void applyNodeChanges(PathNode *node, PathState *state,
                             struct stat stats);

#endif