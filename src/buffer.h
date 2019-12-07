/** @file
  Declares functions for allocating buffers and frees them at exit.
*/

#ifndef NANO_BACKUP_SRC_BUFFER_H
#define NANO_BACKUP_SRC_BUFFER_H

#include <stddef.h>

/** A struct associating memory with its allocated capacity. */
typedef struct
{
  char *data;      /**< The allocated memory. */
  size_t capacity; /**< The capacity of the allocated memory. */
}Buffer;

extern void bufferEnsureCapacity(Buffer **buffer_ptr, size_t capacity);

#endif
