#ifndef NANO_BACKUP_SRC_COLORS_H
#define NANO_BACKUP_SRC_COLORS_H

#include <stdio.h>

/** Defines various text color attributes. */
typedef enum
{
  TC_none,
  TC_bold,
  TC_red,
  TC_red_bold,
  TC_green,
  TC_green_bold,
  TC_yellow,
  TC_yellow_bold,
  TC_blue,
  TC_blue_bold,
  TC_magenta,
  TC_magenta_bold,
  TC_cyan,
  TC_cyan_bold,
} TextColor;

extern void colorPrintf(FILE *stream, TextColor color, const char *format,
                        ...)
#ifdef __GNUC__
  __attribute__((format(printf, 3, 4)))
#endif
  ;

#endif
