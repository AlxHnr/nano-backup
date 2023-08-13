#include "allocator.h"

#include <stdlib.h>

#include "test.h"

static void *testAllocator(Allocator *a)
{
  assert_error(allocate(a, 0), "unable to allocate 0 bytes");

  char *data = allocate(a, 2048);
  assert_true(data != NULL);

  data[12] = '\n';
  assert_true(data[12] == '\n');

  return data;
}

int main(void)
{
  testGroupStart("allocate(): allocation failure");
  {
    Allocator *a = allocatorWrapAlwaysFailing();
    assert_error(allocate(a, 1272), "out of memory: failed to allocate 1272 bytes");
  }
  testGroupEnd();

  testGroupStart("allocate(): wrapping malloc");
  free(testAllocator(allocatorWrapMalloc()));
  testGroupEnd();

  testGroupStart("allocate(): wrapping region");
  {
    CR_Region *r = CR_RegionNew();
    Allocator *a = allocatorWrapRegion(r);
    testAllocator(a);
    testAllocator(a);

    /* Attach another allocator to the region. */
    testAllocator(allocatorWrapRegion(r));
    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("allocate(): wrapping one single growable buffer");
  {
    CR_Region *r = CR_RegionNew();
    testAllocator(allocatorWrapOneSingleGrowableBuffer(r));
    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("allocate(): reuse single growable buffer");
  {
    CR_Region *r = CR_RegionNew();
    Allocator *a = allocatorWrapOneSingleGrowableBuffer(r);

    void *data1 = allocate(a, 1024);
    void *data2 = allocate(a, 48);
    void *data3 = allocate(a, 91);
    assert_true(data1 != NULL);
    assert_true(data2 != NULL);
    assert_true(data3 != NULL);

    /* Ensure the same buffer gets returned when growing can be avoided. */
    assert_true(data1 == data2);
    assert_true(data1 == data3);

    char *data = data1;
    data[39] = 'o';
    assert_true(data[39] == 'o');

    CR_RegionRelease(r);
  }
  testGroupEnd();
}
