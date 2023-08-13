#ifndef NANO_BACKUP_SRC_REGEX_POOL_H
#define NANO_BACKUP_SRC_REGEX_POOL_H

#include <regex.h>

#include "str.h"

extern const regex_t *rpCompile(StringView expression,
                                StringView file_name, size_t line_nr);

#endif
