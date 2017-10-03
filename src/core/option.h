#ifndef REDREAM_OPTION_H
#define REDREAM_OPTION_H

#include <string.h>
#include "core/constructor.h"
#include "core/list.h"

#define OPTION_MAX_LENGTH 1024

#define DECLARE_OPTION_INT(name) extern int OPTION_##name;
#define DECLARE_OPTION_STRING(name) \
  extern char OPTION_##name[OPTION_MAX_LENGTH];

#define DEFINE_OPTION_INT_EXT(name, value, desc, flags)                \
  int OPTION_##name;                                                   \
  static struct option OPTION_T_##name = {OPTION_INT,     #name, desc, \
                                          &OPTION_##name, flags, {0}}; \
  CONSTRUCTOR(OPTION_REGISTER_##name) {                                \
    *(int *)(&OPTION_##name) = value;                                  \
    options_register(&OPTION_T_##name);                                \
  }                                                                    \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {                               \
    options_unregister(&OPTION_T_##name);                              \
  }
#define DEFINE_OPTION_STRING_EXT(name, value, desc, flags)             \
  char OPTION_##name[OPTION_MAX_LENGTH];                               \
  static struct option OPTION_T_##name = {OPTION_STRING,  #name, desc, \
                                          &OPTION_##name, flags, {0}}; \
  CONSTRUCTOR(OPTION_REGISTER_##name) {                                \
    strncpy(OPTION_##name, value, OPTION_MAX_LENGTH);                  \
    options_register(&OPTION_T_##name);                                \
  }                                                                    \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {                               \
    options_unregister(&OPTION_T_##name);                              \
  }

#define DEFINE_PERSISTENT_OPTION_INT(name, value, desc) \
  DEFINE_OPTION_INT_EXT(name, value, desc, OPTION_PERSIST)
#define DEFINE_PERSISTENT_OPTION_STRING(name, value, desc) \
  DEFINE_OPTION_STRING_EXT(name, value, desc, OPTION_PERSIST)
#define DEFINE_OPTION_INT(name, value, desc) \
  DEFINE_OPTION_INT_EXT(name, value, desc, 0)
#define DEFINE_OPTION_STRING(name, value, desc) \
  DEFINE_OPTION_STRING_EXT(name, value, desc, 0)

enum option_type {
  OPTION_INT,
  OPTION_STRING,
};

enum {
  OPTION_PERSIST = 0x1,
};

struct option {
  enum option_type type;
  const char *name;
  const char *desc;
  void *storage;
  int flags;
  struct list_node it;
};

void options_register(struct option *option);
void options_unregister(struct option *option);

int options_parse(int *argc, char ***argv);
int options_read(const char *filename);
int options_write(const char *filename);

#endif
