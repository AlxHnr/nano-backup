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
  Implements functions for allocating memory from an internal memory pool.
  This pool should only be used for data that lives as long as the entire
  program.
*/

#include "memory-pool.h"

#include <stdlib.h>

#include "error-handling.h"
#include "safe-wrappers.h"

/** Stores the references to all chunks. */
static char **chunks = NULL;
static size_t chunks_allocated = 0;

/** A pointer to the beginning of the current chunk. */
static char *chunk = NULL;
static size_t chunk_used = 0;
static size_t chunk_size = 0;

/** A helper variable for calculating the size of the next chunk. */
static size_t least_chunk_size = 1024;

/** Frees the internal memory pool. */
static void freeMemoryPool(void)
{
  for(size_t index = 0; index < chunks_allocated; index++)
  {
    free(chunks[index]);
  }

  free(chunks);
}

/** Allocates memory from the internal memory pool. This function will
  terminate the program on failure.

  @param size The amount of bytes to allocate. Must be greater than 0,
  otherwise the program will be terminated with an error message.

  @return A pointer to the allocated and uninitialized data inside the
  memory pool. This data should not be freed or reallocated by the caller.
*/
void *mpAlloc(size_t size)
{
  if(size == 0)
  {
    die("memory pool: unable to allocate 0 bytes");
  }

  if(size > chunk_size - chunk_used)
  {
    if(chunks == NULL) atexit(freeMemoryPool);

    /* Add new slot to chunk array. */
    size_t new_chunk_array_size =
      sSizeMul(sSizeAdd(chunks_allocated, 1), sizeof(chunks));

    chunks = sRealloc(chunks, new_chunk_array_size);

    /* Allocate new chunk. */
    least_chunk_size = sSizeMul(least_chunk_size, 2);
    chunk_size = size > least_chunk_size ? size : least_chunk_size;
    chunk = sMalloc(chunk_size);

    /* Update static variables. */
    chunks[chunks_allocated] = chunk;
    chunks_allocated++;
    chunk_used = size;

    return chunk;
  }
  else
  {
    void *data = &chunk[chunk_used];
    chunk_used += size;
    return data;
  }
}
