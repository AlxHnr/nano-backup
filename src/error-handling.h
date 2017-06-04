/** @file
  Declares various functions for error handling.
*/

#ifndef NANO_BACKUP_ERROR_HANDLING_H
#define NANO_BACKUP_ERROR_HANDLING_H

#ifdef __GNUC__
#define DIE_FUNCTION_ATTRIBUTES \
  __attribute__((noreturn, format(printf, 1, 2)))
#else
#define DIE_FUNCTION_ATTRIBUTES
#endif

extern void die(const char *format, ...)      DIE_FUNCTION_ATTRIBUTES;
extern void dieErrno(const char *format, ...) DIE_FUNCTION_ATTRIBUTES;

#undef DIE_FUNCTION_ATTRIBUTES

#endif
