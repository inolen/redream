#ifndef REDREAM_STRING_H
#define REDREAM_STRING_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "core/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_STRCASECMP
#define strcasecmp _stricmp
#endif

#ifndef HAVE_STRNSTR
char *strnstr(const char *s1, const char *s2, size_t n);
#endif

int strnrep(char *dst, size_t dst_size, const char *token, size_t token_len,
            const char *value, size_t value_len);

int xtoi(char c);

#ifdef __cplusplus
}
#endif

#endif
