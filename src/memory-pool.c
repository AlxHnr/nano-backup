/** @file
  Implements functions for allocating memory from an internal memory pool.
  This pool should only be used for data that lives as long as the entire
  program.
*/

#include "memory-pool.h"

#include "CRegion/global-region.h"
#include "CRegion/region.h"

/** Allocates memory from the internal memory pool. This function will
  terminate the program on failure.

  @param size The amount of bytes to allocate. Must be greater than 0,
  otherwise the program will be terminated with an error message.

  @return A pointer to the allocated and uninitialized data inside the
  memory pool. This data should not be freed or reallocated by the caller.
*/
void *mpAlloc(size_t size)
{
  return CR_RegionAlloc(CR_GetGlobalRegion(), size);
}
