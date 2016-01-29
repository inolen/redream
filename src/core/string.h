#ifndef DREAVM_STRING_H
#define DREAVM_STRING_H

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include "core/config.h"

namespace dvm {

#ifndef HAVE_STRCASECMP
#define strcasecmp stricmp
#endif

#ifndef HAVE_STRNSTR
char *strnstr(const char *s1, const char *s2, size_t n);
#endif

int strnrep(char *dst, size_t dst_size, const char *token, size_t token_len,
            const char *value, size_t value_len);

int xtoi(char c);

}

#endif
