/** @file
  Declares functions for allocating resizable data from regions.
*/

#ifndef CREGION_SRC_ALLOC_GROWABLE_H
#define CREGION_SRC_ALLOC_GROWABLE_H

#include "region.h"

extern void *CR_RegionAllocGrowable(CR_Region *r, size_t size);
extern void *CR_EnsureCapacity(void *ptr, size_t size)
#ifdef __GNUC__
__attribute__((warn_unused_result))
#endif
  ;

#endif
