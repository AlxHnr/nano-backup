/** @file
  Tests the internal memory pool.
*/

#include "memory-pool.h"

#include "test.h"

int main(void)
{
  testGroupStart("mpAlloc()");
  void *data59    = mpAlloc(59);
  void *data1     = mpAlloc(1);
  void *data123   = mpAlloc(123);
  void *data4096  = mpAlloc(4096);

  void *data32    = mpAlloc(32);
  void *data80    = mpAlloc(80);
  void *data16384 = mpAlloc(16384);

  assert_true(data59    != NULL);
  assert_true(data1     != NULL);
  assert_true(data123   != NULL);
  assert_true(data4096  != NULL);

  assert_true(data32    != NULL);
  assert_true(data80    != NULL);
  assert_true(data16384 != NULL);

  /* Assert that all the pointer defined above don't point to the same
     addresses. */
  assert_true(data59 != data1);
  assert_true(data59 != data123);
  assert_true(data59 != data4096);
  assert_true(data59 != data32);
  assert_true(data59 != data80);
  assert_true(data59 != data16384);

  assert_true(data1 != data123);
  assert_true(data1 != data4096);
  assert_true(data1 != data32);
  assert_true(data1 != data80);
  assert_true(data1 != data16384);

  assert_true(data123 != data4096);
  assert_true(data123 != data32);
  assert_true(data123 != data80);
  assert_true(data123 != data16384);

  assert_true(data4096 != data32);
  assert_true(data4096 != data80);
  assert_true(data4096 != data16384);

  assert_true(data32 != data80);
  assert_true(data32 != data16384);

  assert_true(data80 != data16384);

  /* mpAlloc() must fail if called with 0 as argument. */
  assert_error(mpAlloc(0), "memory pool: unable to allocate 0 bytes");
  testGroupEnd();
}
