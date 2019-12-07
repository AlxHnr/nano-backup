/** @file
  Declares a function for accessing a global region, which is bound to the
  lifetime of the entire program.
*/

#ifndef CREGION_SRC_GLOBAL_REGION_H
#define CREGION_SRC_GLOBAL_REGION_H

#include "region.h"

extern CR_Region *CR_GetGlobalRegion(void);

#endif
