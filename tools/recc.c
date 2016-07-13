#include "core/log.h"
#include "core/option.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/conversion_elimination_pass.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "sys/filesystem.h"

DEFINE_OPTION_BOOL(help, false, "Show help");
DEFINE_OPTION_STRING(pass, "lse,cve,dce,ra",
                     "Comma-separated list of passes to run");
DEFINE_OPTION_BOOL(print_after_all, true, "Print IR after each pass");
DEFINE_OPTION_BOOL(stats, true, "Display pass stats");

DEFINE_STAT(num_instrs, "Total number of instructions");
DEFINE_STAT(num_instrs_removed, "Number of instructions removed");

static uint8_t ir_buffer[1024 * 1024];

static int get_num_instrs(const struct ir *ir) {
  int n = 0;

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    ((void)instr);
    n++;
  }

  return n;
}

static void process_file(const char *filename, bool disable_ir_dump) {
  struct ir ir = {0};
  ir.buffer = ir_buffer;
  ir.capacity = sizeof(ir_buffer);

  // read in the input ir
  FILE *input = fopen(filename, "r");
  CHECK(input);
  int r = ir_read(input, &ir);
  fclose(input);
  CHECK(r);

  int num_instrs_before = get_num_instrs(&ir);

  // run optimization passes
  char passes[MAX_OPTION_LENGTH];
  strncpy(passes, OPTION_pass, sizeof(passes));

  char *name = strtok(passes, ",");
  while (name) {
    if (!strcmp(name, "lse")) {
      lse_run(&ir);
    } else if (!strcmp(name, "cve")) {
      cve_run(&ir);
    } else if (!strcmp(name, "dce")) {
      dce_run(&ir);
    } else if (!strcmp(name, "ra")) {
      ra_run(&ir, x64_registers, x64_num_registers);
    } else {
      LOG_WARNING("Unknown pass %s", name);
    }

    // print IR after each pass if requested
    if (!disable_ir_dump && OPTION_print_after_all) {
      LOG_INFO("===-----------------------------------------------------===");
      LOG_INFO("IR after %s", name);
      LOG_INFO("===-----------------------------------------------------===");
      ir_write(&ir, stdout);
      LOG_INFO("");
    }

    name = strtok(NULL, ",");
  }

  int num_instrs_after = get_num_instrs(&ir);

  // print out the final IR
  if (!disable_ir_dump && !OPTION_print_after_all) {
    ir_write(&ir, stdout);
  }

  STAT_num_instrs += num_instrs_before;
  STAT_num_instrs_removed += num_instrs_before - num_instrs_after;
}

static void process_dir(const char *path) {
  DIR *dir = opendir(path);

  if (dir) {
    struct dirent *ent = readdir(dir);

    while (ent) {
      if (!(ent->d_type & DT_REG)) {
        continue;
      }

      char filename[PATH_MAX];
      snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "%s", path,
               ent->d_name);

      LOG_INFO("processing %s", filename);

      process_file(filename, true);
    }

    closedir(dir);
  }
}

int main(int argc, char **argv) {
  options_parse(&argc, &argv);

  if (OPTION_help) {
    options_print_help();
    return EXIT_SUCCESS;
  }

  const char *path = argv[1];

  if (fs_isfile(path)) {
    process_file(path, false);
  } else {
    process_dir(path);
  }

  if (OPTION_stats) {
    pass_stat_print_all();
  }

  return EXIT_SUCCESS;
}
