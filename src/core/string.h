#ifndef REDREAM_STRING_H
#define REDREAM_STRING_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "core/config.h"
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifndef HAVE_STRCASECMP
#define strcasecmp _stricmp
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t max_len);
#endif

#ifndef HAVE_STRNSTR
char *strnstr(const char *s1, const char *s2, size_t n);
#endif

void strncpy_pad_spaces(char *dst, const char *str, int size);
void strncpy_trim_space(char *dst, const char *str, int size);

int strnrep(char *dst, size_t dst_size, const char *token, size_t token_len,
            const char *value, size_t value_len);

int xtoi(char c);

#endif
