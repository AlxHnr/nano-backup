/** @file
  Declares functions for printing colored text.
*/

#ifndef NANO_BACKUP_COLORS_H
#define NANO_BACKUP_COLORS_H

#include <stdio.h>

/** Defines various text color attributes. */
typedef enum
{
  TC_red,          /**< Red text. */
  TC_red_bold,     /**< Red and bold text. */
  TC_green,        /**< Green text. */
  TC_green_bold,   /**< Green and bold text. */
  TC_yellow,       /**< Yellow text. */
  TC_yellow_bold,  /**< Yellow and bold text. */
  TC_blue,         /**< Blue text. */
  TC_blue_bold,    /**< Blue and bold text. */
  TC_magenta,      /**< Magenta text. */
  TC_magenta_bold, /**< Magen and bold ta text. */
  TC_cyan,         /**< Cyan text. */
  TC_cyan_bold,    /**< Cyan and bold text. */
  TC_white,        /**< White text. */
  TC_white_bold,   /**< White and bold text. */
}TextColor;

extern void colorPrintf(FILE *stream, TextColor color,
                        const char *format, ...)
#ifdef __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
  ;

#endif
