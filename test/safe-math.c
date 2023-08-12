#include "safe-math.h"

#include "test.h"

int main(void)
{
  testGroupStart("sSizeAdd()");
  const char *expected_error = "overflow calculating object size";
  assert_true(sSizeAdd(0, 0) == 0);
  assert_true(sSizeAdd(2, 3) == 5);
  assert_true(sSizeAdd(50, 75) == 125);
  assert_true(sSizeAdd(65, SIZE_MAX - 65) == SIZE_MAX);
  assert_error(sSizeAdd(SIZE_MAX, SIZE_MAX), expected_error);
  assert_error(sSizeAdd(512, SIZE_MAX - 90), expected_error);
  assert_error(sSizeAdd(SIZE_MAX, 1), expected_error);
  testGroupEnd();

  testGroupStart("sSizeMul()");
  assert_true(sSizeMul(0, 5) == 0);
  assert_true(sSizeMul(5, 3) == 15);
  assert_true(sSizeMul(3, 5) == 15);
  assert_true(sSizeMul(70, 80) == 5600);
  assert_true(sSizeMul(0, 0) == 0);
  assert_true(sSizeMul(3, 0) == 0);
  assert_true(sSizeMul(2348, 0) == 0);
  assert_true(sSizeMul(SIZE_MAX, 0) == 0);
  assert_true(sSizeMul(SIZE_MAX, 1));
  assert_error(sSizeMul(SIZE_MAX, 25), expected_error);
  assert_error(sSizeMul(SIZE_MAX - 80, 295), expected_error);
  testGroupEnd();

  testGroupStart("sUint64Add()");
  const char *expected_error_u64 = "overflow calculating unsigned 64-bit value";
  assert_true(sUint64Add(0, 0) == 0);
  assert_true(sUint64Add(2, 3) == 5);
  assert_true(sUint64Add(50, 75) == 125);
  assert_true(sUint64Add(65, UINT64_MAX - 65) == UINT64_MAX);
  assert_error(sUint64Add(UINT64_MAX, UINT64_MAX), expected_error_u64);
  assert_error(sUint64Add(512, UINT64_MAX - 90), expected_error_u64);
  assert_error(sUint64Add(UINT64_MAX, 1), expected_error_u64);
  testGroupEnd();
}
