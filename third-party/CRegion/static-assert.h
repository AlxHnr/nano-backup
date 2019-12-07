/** @file
  Declares a static assert macro.
*/

#ifndef CREGION_SRC_STATIC_ASSERT_H
#define CREGION_SRC_STATIC_ASSERT_H

/** Static assert macro for use inside functions. */
#define CR_StaticAssert(condition) \
  do{ static const char static_assert_failed[(condition)?1:-1]; \
    (void)static_assert_failed; }while(0)

#endif
