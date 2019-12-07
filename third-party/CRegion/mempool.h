/** @file
  Declares functions for reusing memory allocated from regions.
*/

#ifndef CREGION_SRC_MEMPOOL_H
#define CREGION_SRC_MEMPOOL_H

#include "region.h"

/* A memory pool. */
typedef struct CR_Mempool CR_Mempool;

/** A destructor function which is allowed to fail by calling exit(). The
  return value of this function will be ignored. It has the type int to be
  incompatible with CR_ReleaseCallback. */
typedef int CR_FailableDestructor(void *data);

extern CR_Mempool *CR_MempoolNew(CR_Region *r, size_t object_size,
                                 CR_FailableDestructor *explicit_destructor,
                                 CR_ReleaseCallback *implicit_destructor);
extern void *CR_MempoolAlloc(CR_Mempool *mp);
extern void CR_EnableObjectDestructor(void *ptr);
extern void CR_DestroyObject(void *ptr);

#endif
