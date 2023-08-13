#ifndef NANO_BACKUP_SRC_ALLOCATOR_H
#define NANO_BACKUP_SRC_ALLOCATOR_H

#include <stddef.h>

#include "CRegion/region.h"

typedef struct Allocator Allocator;

extern void *allocate(Allocator *a, size_t size);
extern Allocator *allocatorWrapMalloc(void);
extern Allocator *allocatorWrapRegion(CR_Region *r);
extern Allocator *allocatorWrapOneSingleGrowableBuffer(CR_Region *r);
extern Allocator *allocatorWrapAlwaysFailing(void);

#endif
