/*
  Copyright (c) 2016 Alexander Heinrich <alxhnr@nudelpost.de>

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
