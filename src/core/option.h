#ifndef REDREAM_OPTION_H
#define REDREAM_OPTION_H

#include <string.h>
#include "core/constructor.h"
#include "core/list.h"

#define OPTION_MAX_LENGTH 1024

#define DECLARE_OPTION_INT(name) \
  extern int OPTION_##name;      \
  extern int OPTION_##name##_dirty;

#define DECLARE_OPTION_STRING(name)             \
  extern char OPTION_##name[OPTION_MAX_LENGTH]; \
  extern int OPTION_##name##_dirty;

#define DEFINE_OPTION_INT_EXT(name, value, desc, flags)                \
  int OPTION_##name;                                                   \
  int OPTION_##name##_dirty;                                           \
  static struct option OPTION_##name##_struct = {                      \
      OPTION_INT, #name, desc, &OPTION_##name, &OPTION_##name##_dirty, \
      flags,      {0}};                                                \
  CONSTRUCTOR(OPTION_REGISTER_##name) {                                \
    *(int *)(&OPTION_##name) = value;                                  \
    option_register(&OPTION_##name##_struct);                          \
  }                                                                    \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {                               \
    option_unregister(&OPTION_##name##_struct);                        \
  }
#define DEFINE_OPTION_STRING_EXT(name, value, desc, flags) \
  char OPTION_##name[OPTION_MAX_LENGTH];                   \
  int OPTION_##name##_dirty;                               \
  static struct option OPTION_##name##_struct = {          \
      OPTION_STRING,          #name, desc, &OPTION_##name, \
      &OPTION_##name##_dirty, flags, {0}};                 \
  CONSTRUCTOR(OPTION_REGISTER_##name) {                    \
    strncpy(OPTION_##name, value, OPTION_MAX_LENGTH);      \
    option_register(&OPTION_##name##_struct);              \
  }                                                        \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {                   \
    option_unregister(&OPTION_##name##_struct);            \
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
  void *value;
  int *dirty;
  int flags;
  struct list_node it;
};

void option_register(struct option *option);
void option_unregister(struct option *option);

int options_parse(int *argc, char ***argv);
int options_read(const char *filename);
int options_write(const char *filename);

#endif
