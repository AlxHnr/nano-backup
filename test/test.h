/** @file
  Declares functions for testing code. This header should only be included
  by tests.
*/

#ifndef NANO_BACKUP_TEST_TEST_H
#define NANO_BACKUP_TEST_TEST_H

#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>

/* This test suite uses longjump to catch and handle calls to die().
   Unfortunately GCC tries to inline as much as possible into assert
   statements, which causes self-imposed warnings that don't build with
   -Werror. */
#if defined __GNUC__ && !defined __clang__
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

extern void testGroupStart(const char *name);
extern void testGroupEnd(void);

extern void getLastErrorMessage(char *out, size_t out_size);

/** Asserts that the given expression evaluates to true. It will catch
  calls to die(). */
#define assert_true(expression) \
  test_catch_die = true; \
  if(setjmp(test_jump_buffer) == 0) \
  { \
    if(!(expression)) \
    { \
      dieTest("%s: line %i: assert failed: %s", __FILE__, __LINE__, #expression); \
    } \
  } \
  else \
  { \
    dieTest("%s: line %i: unexpected error: %s", __FILE__, __LINE__, test_error_message); \
  } \
  test_catch_die = false;

/** Asserts that the given expression causes a call to die() with the
  specified error message. */
#define assert_error(expression, message) assert_error_internal(expression, message, false, 0, true);

/** Asserts that the given expression causes a call to dieErrno() with the
  specified error message and specified errno value. */
#define assert_error_errno(expression, message, expected_errno) \
  assert_error_internal(expression, message, false, expected_errno, false);

/** Asserts that the given expression causes a call to die() or dieErrno()
  without checking for a specific error string. */
#define assert_error_any(expression) assert_error_internal(expression, "", true, 0, true);

/* Everything below this line should only be used inside test.c. */

#define assert_error_internal(expression, message, ignore_message, expected_errno, ignore_errno) \
  test_catch_die = true; \
  if(setjmp(test_jump_buffer) == 0) \
  { \
    (void)(expression); \
    dieTest("%s: line %i: expected error: %s", __FILE__, __LINE__, #expression); \
  } \
  else if(ignore_message == false && strcmp(message, test_error_message) != 0) \
  { \
    dieTest("%s: line %i: got wrong error message: \"%s\"\n" \
            "\t\texpected: \"%s\"", \
            __FILE__, __LINE__, test_error_message, message); \
  } \
  else if(ignore_errno == false && errno != expected_errno) \
  { \
    dieTest("%s: line %i: got wrong errno value: %i" \
            ", expected: %i", \
            __FILE__, __LINE__, errno, expected_errno); \
  } \
  errno = 0; \
  test_catch_die = false;

extern jmp_buf test_jump_buffer;
extern char *test_error_message;
extern bool test_catch_die;

extern void dieTest(const char *format, ...)
#ifdef __GNUC__
  __attribute__((noreturn, format(printf, 1, 2)))
#endif
  ;

#endif
