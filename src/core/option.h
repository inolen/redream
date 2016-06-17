#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>
#include <string.h>
#include "core/constructor.h"
#include "core/list.h"

#define DECLARE_OPTION_BOOL(name) extern bool OPTION_##name;

#define DEFINE_OPTION_BOOL(name, value, desc)     \
  bool OPTION_##name;                             \
  static struct option OPTION_T_##name = {        \
      OPT_BOOL, #name, desc, &OPTION_##name, {}}; \
  CONSTRUCTOR(OPTION_REGISTER_##name) {           \
    *(bool *)(&OPTION_##name) = value;            \
    option_register(&OPTION_T_##name);            \
  }                                               \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {          \
    option_unregister(&OPTION_T_##name);          \
  }

#define DECLARE_OPTION_INT(name) extern int OPTION_##name;

#define DEFINE_OPTION_INT(name, value, desc)     \
  int OPTION_##name;                             \
  static struct option OPTION_T_##name = {       \
      OPT_INT, #name, desc, &OPTION_##name, {}}; \
  CONSTRUCTOR(OPTION_REGISTER_##name) {          \
    *(int *)(&OPTION_##name) = value;            \
    option_register(&OPTION_T_##name);           \
  }                                              \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {         \
    option_unregister(&OPTION_T_##name);         \
  }

#define DECLARE_OPTION_STRING(name) extern char OPTION_##name[1024];

#define DEFINE_OPTION_STRING(name, value, desc)     \
  char OPTION_##name[1024];                         \
  static struct option OPTION_T_##name = {          \
      OPT_STRING, #name, desc, &OPTION_##name, {}}; \
  CONSTRUCTOR(OPTION_REGISTER_##name) {             \
    strcpy((char *) & OPTION_##name, value);        \
    option_register(&OPTION_T_##name);              \
  }                                                 \
  DESTRUCTOR(OPTION_UNREGISTER_##name) {            \
    option_unregister(&OPTION_T_##name);            \
  }

enum option_type {
  OPT_BOOL,
  OPT_INT,
  OPT_STRING,
};

struct option {
  enum option_type type;
  const char *name;
  const char *desc;
  void *storage;
  struct list_node it;
};

void option_register(struct option *option);
void option_unregister(struct option *option);

void option_parse(int *argc, char ***argv);
void option_print_help();

#endif
