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
  Implements various functions required for testing. This module conflicts
  with error-handling.c, since it implements the same functions in a
  different way.
*/

#include "test.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "colors.h"
#include "error-handling.h"

/** A global jump buffer. It should not be used directly. */
jmp_buf test_jump_buffer;

/** Contains the error message of the last die() invocation or NULL. This
  variable should not be used directly. */
char *test_error_message = NULL;

/** True, if a call to die() or dieErrno() should be handled with a longjmp
  back into the last assert statement. This variable should not be used
  directly. */
bool test_catch_die = false;

/** Frees the memory in test_error_message. */
static void freeTestErrorMessage(void)
{
  free(test_error_message);
}

/** Assigns allocated memory to test_error_message and uses it to store the
  formatted error message. If this function fails to do so, it will
  terminate the test suite with an error message.

  @param format A format string.
  @param arguments An initialised va_list containing all arguments
  specified in the format string.
  @param length The length of the formatted message without the terminating
  null byte.
  @param buffer_length The length of the buffer that should be allocated.
  Must be at least the size of the given length plus 1.
*/
static void populateTestErrorMessage(const char *format, va_list arguments,
                                     int length, size_t buffer_length)
{
  /* Register cleanup function on the first call of this function. */
  if(test_error_message == NULL &&
     atexit(freeTestErrorMessage) != 0)
  {
    dieTest("failed to register function with atexit");
  }

  /* Free the previous memory stored in test_error_message. */
  free(test_error_message);

  test_error_message = malloc(buffer_length);
  if(test_error_message == NULL)
  {
    dieTest("failed to allocate space to store the error message");
  }

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#endif
#endif
  int bytes_copied = vsprintf(test_error_message, format, arguments);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

  if(bytes_copied != length)
  {
    dieTest("failed to copy the error message");
  }
}

/* The next two functions are alternative implementations of the die() and
   dieErrno() functions. If test_catch_die is true, they will store the
   error message in test_error_message and jump back into the last assert
   statement. Otherwise they will terminate the program with failure.
*/

void die(const char *format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  int format_length = vsnprintf(NULL, 0, format, arguments);
  va_end(arguments);

  if(format_length < 0 || format_length == INT_MAX)
  {
    dieTest("failed to calculate the error message length");
  }

  va_start(arguments, format);
  populateTestErrorMessage(format, arguments, format_length,
                           format_length + 1);
  va_end(arguments);

  if(test_catch_die)
  {
    longjmp(test_jump_buffer, 1);
  }
  else
  {
    dieTest("%s", test_error_message);
  }
}

void dieErrno(const char *format, ...)
{
  va_list arguments;

  va_start(arguments, format);
  int format_length = vsnprintf(NULL, 0, format, arguments);
  va_end(arguments);

  size_t errno_string_length = strlen(strerror(errno));

  /* Ensure that the formatted message, ": ", errnos translated string and
     the terminating null byte fit into the string. */
  if(format_length < 0 ||
     errno_string_length > SIZE_MAX - format_length - 3)
  {
    dieTest("failed to calculate the error message length");
  }

  va_start(arguments, format);
  populateTestErrorMessage(format, arguments, format_length,
                           format_length + errno_string_length + 3);
  va_end(arguments);

  test_error_message[format_length] = ':';
  test_error_message[format_length + 1] = ' ';
  strcpy(&test_error_message[format_length + 2], strerror(errno));

  if(test_catch_die)
  {
    longjmp(test_jump_buffer, 1);
  }
  else
  {
    dieTest("%s", test_error_message);
  }
}

/** Prints a fancy error message and terminates the test suite. It takes
  the same arguments as printf(). This function should not be called
  directly.

  @param format A valid formatting string. This string doesn't need to
  contain newlines.
  @param ... Additional arguments.
*/
void dieTest(const char *format, ...)
{
  va_list arguments;
  va_start(arguments, format);

  printf("[");
  colorPrintf(stdout, TC_red_bold, "FAILURE");
  printf("]\n    ");

  if(test_catch_die == false)
  {
    colorPrintf(stdout, TC_red, "unexpected error");
    printf(": ");
  }

  vprintf(format, arguments);
  printf("\n");

  va_end(arguments);

  exit(EXIT_FAILURE);
}

/** Prints a fancy message which indicates that a test group was entered.
  This function must be called before any use of assert_true or
  assert_error, otherwise it will lead to messed up output on the terminal.

  @param name The name of the test group.
*/
void testGroupStart(const char *name)
{
  printf("  Testing %s", name);

  for(size_t index = strlen(name); index < 61; index++)
  {
    fputc('.', stdout);
  }
}

/** Prints a fancy success message. This must be called before another test
  group starts or the program exits. Otherwise it will lead to messed up
  output on the terminal.
*/
void testGroupEnd(void)
{
  printf("[");
  colorPrintf(stdout, TC_green, "success");
  printf("]\n");
}
