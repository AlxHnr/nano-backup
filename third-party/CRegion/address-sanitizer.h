/** @file
 * Contains macros for ASAN support.
 */

#ifndef CREGION_SRC_ADDRESS_SANITIZER_H
#define CREGION_SRC_ADDRESS_SANITIZER_H

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#endif

#ifndef ASAN_POISON_MEMORY_REGION
#define ASAN_POISON_MEMORY_REGION(address, size) \
  ((void)(address), (void)(size))
#endif

#ifndef ASAN_UNPOISON_MEMORY_REGION
#define ASAN_UNPOISON_MEMORY_REGION(address, size) \
  ((void)(address), (void)(size))
#endif

#endif
