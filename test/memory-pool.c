/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

/**
  @file memory-pool.c Tests the internal memory pool.
*/

#include "memory-pool.h"

#include "test.h"

int main(void)
{
  testGroupStart("mpAlloc()");
  void *data59    = mpAlloc(59);
  void *data123   = mpAlloc(123);
  void *data4096  = mpAlloc(4096);

  void *data32    = mpAlloc(32);
  void *data80    = mpAlloc(80);
  void *data16384 = mpAlloc(16384);

  assert_true(data59    != NULL);
  assert_true(data123   != NULL);
  assert_true(data4096  != NULL);

  assert_true(data32    != NULL);
  assert_true(data80    != NULL);
  assert_true(data16384 != NULL);

  /* Assert that all the pointer defined above don't point to the same
     addresses. */
  assert_true(data59 != data123);
  assert_true(data59 != data4096);
  assert_true(data59 != data32);
  assert_true(data59 != data80);
  assert_true(data59 != data16384);

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
