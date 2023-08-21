#include "safe-math.h"

#include "error-handling.h"

/** Adds two sizes and terminates the program on overflows. */
size_t sSizeAdd(const size_t a, const size_t b)
{
  if(a > SIZE_MAX - b)
  {
    die("overflow calculating object size");
  }

  return a + b;
}

/** Multiplies two sizes and terminates the program on overflows. */
size_t sSizeMul(const size_t a, const size_t b)
{
  if(b != 0 && a > SIZE_MAX / b)
  {
    die("overflow calculating object size");
  }

  return a * b;
}

/** Adds two uint64_t values and terminates the program on overflows. */
uint64_t sUint64Add(const uint64_t a, const uint64_t b)
{
  if(a > UINT64_MAX - b)
  {
    die("overflow calculating unsigned 64-bit value");
  }

  return a + b;
}

/** Multiplies two uint64_t values and terminates the program on overflows.
 */
size_t sUint64Mul(const uint64_t a, const uint64_t b)
{
  if(b != 0 && a > UINT64_MAX / b)
  {
    die("overflow calculating unsigned 64-bit value");
  }

  return a * b;
}
