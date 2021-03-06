/** @file
  Implements checked math functions which handle errors by terminating the
  program with an error message.
*/

#include "safe-math.h"

#include "error-handling.h"

/** Adds two sizes and terminates the program on overflows.

  @param a The first summand.
  @param b The second summand.

  @return The sum of a and b.
*/
size_t sSizeAdd(size_t a, size_t b)
{
  if(a > SIZE_MAX - b)
  {
    die("overflow calculating object size");
  }

  return a + b;
}

/** Multiplies two sizes and terminates the program on overflows.

  @param a The first factor.
  @param b The second factor.

  @return The product of a and b.
*/
size_t sSizeMul(size_t a, size_t b)
{
  if(b != 0 && a > SIZE_MAX/b)
  {
    die("overflow calculating object size");
  }

  return a * b;
}

/** Adds two uint64_t values and terminates the program on overflows.

  @param a The first summand.
  @param b The second summand.

  @return The sum of a and b.
*/
uint64_t sUint64Add(uint64_t a, uint64_t b)
{
  if(a > UINT64_MAX - b)
  {
    die("overflow calculating unsigned 64-bit value");
  }

  return a + b;
}
