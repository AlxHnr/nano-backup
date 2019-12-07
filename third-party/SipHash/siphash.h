#ifndef SIPHASH_H
#define SIPHASH_H

#include <stddef.h>
#include <stdint.h>

extern uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);

#endif
