/** @file
  Declares checked math functions which handle errors by terminating the
  program with an error message.
*/

#ifndef NANO_BACKUP_SRC_SAFE_MATH_H
#define NANO_BACKUP_SRC_SAFE_MATH_H

#include <stddef.h>
#include <stdint.h>

extern size_t sSizeAdd(size_t a, size_t b);
extern size_t sSizeMul(size_t a, size_t b);
extern uint64_t sUint64Add(uint64_t a, uint64_t b);

#endif
