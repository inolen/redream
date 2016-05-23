#include <stdbool.h>
#include <string.h>
#include "core/string.h"

#ifndef HAVE_STRNSTR
char *strnstr(const char *s1, const char *s2, size_t n) {
  size_t len = strlen(s2);

  if (!len) {
    return const_cast<char *>(s1);
  }

  while (n >= len) {
    if (!memcmp(s1, s2, len)) {
      return const_cast<char *>(s1);
    }
    n--;
    s1++;
  }

  return nullptr;
}
#endif

int strnrep(char *dst, size_t dst_size, const char *token, size_t token_len,
            const char *value, size_t value_len) {
  char *end = dst + dst_size;

  while (true) {
    char *ptr = strnstr(dst, token, dst_size);

    if (!ptr) {
      break;
    }

    // move substring starting at the end of the token to the end of where the
    // new value will be)
    size_t dst_len = strnlen(dst, dst_size);
    size_t move_size = (dst_len + 1) - ((ptr - dst) + token_len);

    if (ptr + value_len + move_size > end) {
      return -1;
    }

    memmove(ptr + value_len, ptr + token_len, move_size);

    // copy new value into token position
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
