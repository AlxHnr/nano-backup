/** @file
  Implements checked math functions which handle errors by terminating the
  program with an error message.
*/

#include "safe-math.h"

#include <stdint.h>

#include "error-handling.h"

/** Adds two sizes and terminates the program on overflows.

  @param a The first summand.
  @param b The second summand.

  @return The sum of a and b.
*/
size_t CR_SafeAdd(size_t a, size_t b)
{
  if(a > SIZE_MAX - b)
  {
    CR_ExitFailure("overflow calculating object size");
  }

  return a + b;
}

/** Multiplies two sizes and terminates the program on overflows.

  @param a The first factor.
  @param b The second factor.

  @return The product of a and b.
*/
size_t CR_SafeMultiply(size_t a, size_t b)
{
  if(b != 0 && a > SIZE_MAX/b)
  {
    CR_ExitFailure("overflow calculating object size");
  }

  return a * b;
}
