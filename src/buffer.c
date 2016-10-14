/*
  Copyright (c) 2016 Alexander Heinrich

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
  Implements allocation of buffers which get freed at exit.
*/

#include "buffer.h"

#include <stdlib.h>

#include "safe-wrappers.h"

static Buffer **buffers = NULL;
static size_t buffers_allocated = 0;

/** Frees all allocated buffers. */
static void freeBuffers(void)
{
  for(size_t index = 0; index < buffers_allocated; index++)
  {
    free(buffers[index]->data);
    free(buffers[index]);
  }

  free(buffers);
}

/** Ensures that the given buffer contains at least the specified capacity,
  and terminates the program on allocation failure.

  @param buffer_ptr The address of a pointer to a Buffer. If it points to
  a NULL pointer, both the buffer and its data will be allocated and stored
  in that pointer. In that case it should not be freed by the caller,
  because it gets freed automatically when the program terminates.
  @param capacity The expected capacity.
*/
void bufferEnsureCapacity(Buffer **buffer_ptr, size_t capacity)
{
  Buffer *buffer = *buffer_ptr;

  if(buffer == NULL)
  {
    if(buffers == NULL) atexit(freeBuffers);

    size_t new_buffer_array_size =
      sSizeMul(sSizeAdd(buffers_allocated, 1), sizeof *buffers);
    buffers = sRealloc(buffers, new_buffer_array_size);

    buffer = sMalloc(sizeof *buffer);
    buffer->data = sMalloc(capacity);
    buffer->capacity = capacity;

    buffers[buffers_allocated] = buffer;
    buffers_allocated++;

    *buffer_ptr = buffer;
  }
  else if(buffer->capacity < capacity)
  {
    buffer->data = sRealloc(buffer->data, capacity);
    buffer->capacity = capacity;
  }
}
