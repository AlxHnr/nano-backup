/** @file
  Declares checked math functions which handle errors by terminating the
  program with an error message.
*/

#ifndef CREGION_SRC_SAFE_MATH_H
#define CREGION_SRC_SAFE_MATH_H

#include <stddef.h>

extern size_t CR_SafeAdd(size_t a, size_t b);
extern size_t CR_SafeMultiply(size_t a, size_t b);

#endif
