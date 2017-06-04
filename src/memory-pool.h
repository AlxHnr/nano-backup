/** @file
  Declares functions for allocating memory from an internal memory pool.
  This pool should only be used for data that lives as long as the entire
  program.
*/

#ifndef NANO_BACKUP_MEMORY_POOL_H
#define NANO_BACKUP_MEMORY_POOL_H

#include <stddef.h>

extern void *mpAlloc(size_t size);

#endif
