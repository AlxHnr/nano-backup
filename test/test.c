/** @file
  Implements various functions required for testing. This module conflicts
  with error-handling.c, since it implements the same functions in a
  different way.
*/

#include "test.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
*/
static void populateTestErrorMessage(const char *format, va_list arguments)
{
  /* Register cleanup function on the first call of this function. */
  if(test_error_message == NULL && atexit(freeTestErrorMessage) != 0)
  {
    dieTest("failed to register function with atexit");
  }

  va_list arguments_copy;
  va_copy(arguments_copy, arguments);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#endif
#endif
  const int length = vsnprintf(NULL, 0, format, arguments_copy);
  va_end(arguments_copy);

  if(length < 0 || length == INT_MAX)
  {
    dieTest("failed to calculate the error message length");
  }

  /* Free the previous memory stored in test_error_message. */
  free(test_error_message);

  test_error_message = malloc(length + 1);
  if(test_error_message == NULL)
  {
    dieTest("failed to allocate space to store the error message");
  }

  const int bytes_copied = vsprintf(test_error_message, format, arguments);
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
  populateTestErrorMessage(format, arguments);
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
  populateTestErrorMessage(format, arguments);
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

  if(!test_catch_die)
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

/** Copy the latest error message into the given output buffer. If no error
  message exist or the message does not fit into the output buffer,
  terminate with an error.

  @param out Buffer to which the error string will be copied, including the
  '\0' byte.
  @param out_size Size of the given buffer.
*/
void getLastErrorMessage(char *out, const size_t out_size)
{
  if(test_error_message == NULL)
  {
    dieTest("getLastErrorMessage(): no current error message");
  }
  if(out == NULL || out_size == 0)
  {
    dieTest("getLastErrorMessage(): arguments are NULL or zero");
  }

  const size_t error_message_length = strlen(test_error_message);
  if(error_message_length > out_size - 1)
  {
    dieTest("getLastErrorMessage(): given out buffer is too small");
  }
  strncpy(out, test_error_message, out_size);
}
