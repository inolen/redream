#include "core/string.h"

#ifndef HAVE_STRNSTR
char *strnstr(const char *big, const char *little, size_t n) {
  size_t len = strlen(little);

  if (!len) {
    return (char *)big;
  }

  while (*big && n >= len) {
    if (!memcmp(big, little, len)) {
      return (char *)big;
    }
    n--;
    big++;
  }

  return NULL;
}
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t max_len) {
  size_t n;
  for (n = 0; *s && n < max_len; s++, n++)
    ;
  return n;
}
#endif

int strnrep(char *dst, size_t dst_size, const char *token, size_t token_len,
            const char *value, size_t value_len) {
  char *end = dst + dst_size;

  while (1) {
    char *ptr = strnstr(dst, token, dst_size);

    if (!ptr) {
      break;
    }

    /* move substring starting at the end of the token to the end of where the
       new value will be) */
    size_t dst_len = strnlen(dst, dst_size);
    size_t move_size = (dst_len + 1) - ((ptr - dst) + token_len);

    if (ptr + value_len + move_size > end) {
      return -1;
    }

    memmove(ptr + value_len, ptr + token_len, move_size);

    /* copy new value into token position */
    memmove(ptr, value, value_len);
  }

  return 0;
}

int xtoi(char c) {
  int i = -1;
  c = tolower(c);
  if (c >= 'a' && c <= 'f') {
    i = 0xa + (c - 'a');
  } else if (c >= '0' && c <= '9') {
    i = c - '0';
  }
  return i;
}
