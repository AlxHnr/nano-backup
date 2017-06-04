/** @file
  Tests functions for allocating buffers.
*/

#include "buffer.h"

#include "test.h"

/** Asserts that the buffers data doesn't get reallocated if the buffer is
  large enough.

  @param buffer A buffer that was already initialized by
  bufferEnsureCapacity().
  @param new_capacity The capacity to pass to bufferEnsureCapacity(). Must
  not be greater than the given buffers capacity.
*/
static void assert_no_realloc(Buffer *buffer, size_t new_capacity)
{
  char *old_data = buffer->data;
  size_t old_capacity = buffer->capacity;

  assert_true(new_capacity <= old_capacity);
  bufferEnsureCapacity(&buffer, new_capacity);

  assert_true(buffer != NULL);
  assert_true(buffer->data == old_data);
  assert_true(buffer->capacity == old_capacity);
}

/** Performs various tests on a dummy buffer. */
static void test_new_buffer(void)
{
  Buffer *buffer = NULL;

  bufferEnsureCapacity(&buffer, 1);
  assert_true(buffer != NULL);
  assert_true(buffer->data != NULL);
  assert_true(buffer->capacity == 1);

  bufferEnsureCapacity(&buffer, 512);
  assert_true(buffer != NULL);
  assert_true(buffer->data != NULL);
  assert_true(buffer->capacity == 512);

  assert_no_realloc(buffer, 0);
  assert_no_realloc(buffer, 100);
  assert_no_realloc(buffer, 512);
  assert_no_realloc(buffer, 200);

  bufferEnsureCapacity(&buffer, 513);
  assert_true(buffer != NULL);
  assert_true(buffer->data != NULL);
  assert_true(buffer->capacity == 513);

  bufferEnsureCapacity(&buffer, 4096);
  assert_true(buffer != NULL);
  assert_true(buffer->data != NULL);
  assert_true(buffer->capacity == 4096);

  assert_no_realloc(buffer, 12);
  assert_no_realloc(buffer, 1000);
  assert_no_realloc(buffer, 4095);
  assert_no_realloc(buffer, 4096);
  assert_no_realloc(buffer, 0);
  assert_no_realloc(buffer, 64);
}

int main(void)
{
  testGroupStart("bufferEnsureCapacity()");
  test_new_buffer();
  test_new_buffer();
  test_new_buffer();
  test_new_buffer();
  test_new_buffer();
  test_new_buffer();

  Buffer *buffer = NULL;
  assert_error(bufferEnsureCapacity(&buffer, 0), "unable to allocate 0 bytes");
  testGroupEnd();
}
