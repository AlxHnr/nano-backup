/** @file
  Implements a function for accessing a global region, which is bound to
  the lifetime of the entire program.
*/

#include "global-region.h"

/** Returns a global region bound to the lifetime of the program. */
CR_Region *CR_GetGlobalRegion(void)
{
  static CR_Region *r = NULL;
  if(r == NULL)
  {
    r = CR_RegionNew();
  }

  return r;
}
