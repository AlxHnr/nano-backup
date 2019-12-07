/** @file
  Declares a function for handling errors by terminating with failure.
*/

#ifndef CREGION_SRC_ERROR_HANDLING_H
#define CREGION_SRC_ERROR_HANDLING_H

extern void CR_ExitFailure(const char *format, ...)
#ifdef __GNUC__
__attribute__((noreturn, format(printf, 1, 2)))
#endif
  ;

#endif
