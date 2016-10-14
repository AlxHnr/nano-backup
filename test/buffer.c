/*
  Copyright (c) 2016 Alexander Heinrich

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

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
