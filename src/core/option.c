#include "core/log.h"
#include "core/math.h"
#include "core/option.h"
#include "core/string.h"

static list_t s_options;

static option_t *option_find(const char *name) {
  list_for_each_entry(&s_options, option_t, it, opt) {
    if (!strcmp(opt->name, name)) {
      return opt;
    }
  }

  return NULL;
}

void option_register(option_t *option) {
  list_add(&s_options, &option->it);
}

void option_unregister(option_t *option) {
  list_remove(&s_options, &option->it);
}

void option_parse(int *argc, char ***argv) {
  int end = *argc;

  for (int i = 1; i < end;) {
    char *arg = (*argv)[i];

    // move non-option to the end for parsing by the application
    if (arg[0] != '-') {
      (*argv)[i] = (*argv)[end - 1];
      (*argv)[end - 1] = arg;
      end--;
      continue;
    }

    // chomp leading -
    while (arg[0] == '-') {
      arg++;
    }

    // terminate arg and extract value
    char *value = arg;

    while (value[0] && value[0] != '=') {
      value++;
    }

    if (value[0]) {
      *(value++) = 0;
    }

    // lookup the option and assign the parsed value to it
    option_t *opt = option_find(arg);

    if (opt) {
      switch (opt->type) {
        case OPT_BOOL:
          *(bool *)opt->storage = strcmp(value, "false") && strcmp(value, "0");
          break;

        case OPT_INT:
          *(int *)opt->storage = atoi(value);
          break;

        case OPT_STRING:
          strcpy((char *)opt->storage, value);
          break;
      }
    }

    i++;
  }

  *argc -= end - 1;
  *argv += end - 1;
}

void option_print_help() {
  int max_name_width = 0;
  int max_desc_width = 0;

  list_for_each_entry(&s_options, option_t, it, opt) {
    int l = (int)strlen(opt->name);
    max_name_width = MAX(l, max_name_width);

    l = (int)strlen(opt->desc);
    max_desc_width = MAX(l, max_desc_width);
  }

  list_for_each_entry(&s_options, option_t, it, opt) {
    switch (opt->type) {
      case OPT_BOOL:
        LOG_INFO("--%-*s  %-*s  [default %s]", max_name_width, opt->name,
                 max_desc_width, opt->desc,
                 *(bool *)opt->storage ? "true" : "false");
        break;

      case OPT_INT:
        LOG_INFO("--%-*s  %-*s  [default %s]", max_name_width, opt->name,
                 max_desc_width, opt->desc, *(int *)opt->storage);
        break;

      case OPT_STRING:
        LOG_INFO("--%-*s  %-*s  [default %s]", max_name_width, opt->name,
                 max_desc_width, opt->desc, (char *)opt->storage);
        break;
    }
  }
}
