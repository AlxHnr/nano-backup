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

/** @file
  Declares functions for testing code. This header should only be included
  by tests.
*/

#ifndef _NANO_BACKUP_TEST_H_
#define _NANO_BACKUP_TEST_H_

#include <string.h>
#include <setjmp.h>
#include <stdbool.h>

/** Asserts that the given expression evaluates to true. It will catch
  calls to die(). */
#define assert_true(expression) \
  test_catch_die = true; \
  if(setjmp(test_jump_buffer) == 0) { \
    if(!(expression)) { \
      dieTest("%s: line %i: assert failed: %s", \
              __FILE__, __LINE__, #expression); \
    } \
  } else { \
    dieTest("%s: line %i: unexpected error: %s", __FILE__, \
            __LINE__, test_error_message); \
  } test_catch_die = false;

/** Asserts that the given expression causes a call to die() with the
  specified error message. */
#define assert_error(expression, message) \
  test_catch_die = true; \
  if(setjmp(test_jump_buffer) == 0) { \
    (void)(expression); \
    dieTest("%s: line %i: expected error: %s", \
            __FILE__, __LINE__, #expression); \
  } else if(strcmp(message, test_error_message) != 0) { \
    dieTest("%s: line %i: got wrong error message: \"%s\"\n" \
            "\t\texpected: \"%s\"", __FILE__, __LINE__, \
            test_error_message, message); \
  } test_catch_die = false;

extern jmp_buf test_jump_buffer;
extern char *test_error_message;
extern bool test_catch_die;

extern void dieTest(const char *format, ...);

extern void testGroupStart(const char *name);
extern void testGroupEnd(void);

#endif
