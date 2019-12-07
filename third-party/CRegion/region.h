/** @file
  Declares functions for allocating from regions.
*/

#ifndef CREGION_SRC_REGION_H
#define CREGION_SRC_REGION_H

#include <stddef.h>

typedef struct CR_Region CR_Region;

/** A callback function type, which will be called when a region gets
  released. This callback should never call exit(). */
typedef void CR_ReleaseCallback(void *data);

extern CR_Region *CR_RegionNew(void);
extern void *CR_RegionAlloc(CR_Region *r, size_t size);
extern void *CR_RegionAllocUnaligned(CR_Region *r, size_t size);
extern void CR_RegionAttach(CR_Region *r, CR_ReleaseCallback *callback, void *data);
extern void CR_RegionRelease(CR_Region *r);

#endif
