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
  Declares functions for allocating buffers and frees them at exit.
*/

#ifndef NANO_BACKUP_BUFFER_H
#define NANO_BACKUP_BUFFER_H

#include <stddef.h>

/** A struct associating memory with its allocated capacity. */
typedef struct
{
  char *data;      /**< The allocated memory. */
  size_t capacity; /**< The capacity of the allocated memory. */
}Buffer;

extern void bufferEnsureCapacity(Buffer **buffer_ptr, size_t capacity);

#endif
