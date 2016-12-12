#include <ini.h>
#include <stdlib.h>
#include "core/option.h"
#include "core/log.h"
#include "core/math.h"
#include "core/string.h"

static struct list s_options;

static struct option *options_find(const char *name) {
  list_for_each_entry(opt, &s_options, struct option, it) {
    if (!strcmp(opt->name, name)) {
      return opt;
    }
  }

  return NULL;
}

static void options_parse_value(struct option *opt, const char *value) {
  switch (opt->type) {
    case OPT_INT: {
      if (!strcmp(value, "false")) {
        *(int *)opt->storage = 0;
      } else if (!strcmp(value, "true")) {
        *(int *)opt->storage = 1;
      } else {
        *(int *)opt->storage = atoi(value);
      }
    } break;

    case OPT_STRING:
      strncpy((char *)opt->storage, value, MAX_OPTION_LENGTH);
      break;
  }
}

static const char *options_format_value(struct option *opt) {
  static char value[MAX_OPTION_LENGTH];

  switch (opt->type) {
    case OPT_INT:
      snprintf(value, sizeof(value), "%d", *(int *)opt->storage);
      return value;

    case OPT_STRING:
      return (char *)opt->storage;
  }

  return NULL;
}

void options_register(struct option *option) {
  list_add(&s_options, &option->it);
}

void options_unregister(struct option *option) {
  list_remove(&s_options, &option->it);
}

void options_parse(int *argc, char ***argv) {
  int end = *argc;

  for (int i = 1; i < end;) {
    char *arg = (*argv)[i];

    /* move non-option to the end for parsing by the application */
    if (arg[0] != '-') {
      (*argv)[i] = (*argv)[end - 1];
      (*argv)[end - 1] = arg;
      end--;
      continue;
    }

    /* chomp leading - */
    while (arg[0] == '-') {
      arg++;
    }

    /* terminate arg and extract value */
    char *value = arg;

    while (value[0] && value[0] != '=') {
      value++;
    }

    if (value[0]) {
      *(value++) = 0;
    }

    /* lookup the option and assign the parsed value to it */
    struct option *opt = options_find(arg);

    if (opt) {
      options_parse_value(opt, value);
    }

    i++;
  }

  *argc -= end - 1;
  *argv += end - 1;
}

static int options_ini_handler(void *user, const char *section,
                               const char *name, const char *value) {
  struct option *opt = options_find(name);

  if (opt) {
    options_parse_value(opt, value);
  }

  return 0;
}

int options_read(const char *filename) {
  return ini_parse(filename, options_ini_handler, NULL) >= 0;
}

int options_write(const char *filename) {
  FILE *output = fopen(filename, "wt");

  if (!output) {
    return 0;
  }

  list_for_each_entry(opt, &s_options, struct option, it) {
    fprintf(output, "%s: %s\n", opt->name, options_format_value(opt));
  }

  fclose(output);

  return 1;
}

void options_print_help() {
  int max_name_width = 0;
  int max_desc_width = 0;

  list_for_each_entry(opt, &s_options, struct option, it) {
    if (opt->desc == OPTION_HIDDEN) {
      continue;
    }

    int l = (int)strlen(opt->name);
    max_name_width = MAX(l, max_name_width);

    l = (int)strlen(opt->desc);
    max_desc_width = MAX(l, max_desc_width);
  }

  list_for_each_entry(opt, &s_options, struct option, it) {
    if (opt->desc == OPTION_HIDDEN) {
      continue;
    }

    LOG_INFO("--%-*s  %-*s  %s", max_name_width, opt->name, max_desc_width,
             opt->desc, options_format_value(opt));
  }
}
