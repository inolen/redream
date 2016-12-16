#include "core/log.h"
#include "core/option.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/conversion_elimination_pass.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/expression_simplification_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "jit/jit.h"
#include "sys/filesystem.h"

DEFINE_OPTION_INT(help, 0, "Show help");
DEFINE_OPTION_STRING(pass, "lse,cve,esimp,dce,ra",
                     "Comma-separated list of passes to run");
DEFINE_OPTION_INT(stats, 1, "Print pass stats");
DEFINE_OPTION_INT(print_after_all, 1, "Print IR after each pass");

DEFINE_STAT(guest_instrs, "Guest instructions");
DEFINE_STAT(host_instrs, "Host instructions");
DEFINE_STAT(ir_instrs, "IR instructions");
DEFINE_STAT(ir_instrs_removed, "IR instructions removed");

static uint8_t ir_buffer[1024 * 1024];
static uint8_t code[1024 * 1024];
static int code_size = sizeof(code);
static int stack_size = 1024;

static int get_num_instrs(const struct ir *ir) {
  int n = 0;

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    ((void)instr);
    n++;
  }

  return n;
}

static void process_file(struct jit *jit, const char *filename,
                         int disable_dumps) {
  struct ir ir = {0};
  ir.buffer = ir_buffer;
  ir.capacity = sizeof(ir_buffer);

  /* read in the input ir */
  FILE *input = fopen(filename, "r");
  CHECK(input);
  int r = ir_read(input, &ir);
  fclose(input);
  CHECK(r);

  /* calculate number of guest instructions */
  int guest_num_instrs = 0;
  list_for_each_entry(instr, &ir.instrs, struct ir_instr, it) {
    if (instr->op == OP_DEBUG_INFO) {
      guest_num_instrs++;
    }
  }

  /* run optimization passes */
  char passes[MAX_OPTION_LENGTH];
  strncpy(passes, OPTION_pass, sizeof(passes));

  int num_instrs_before = get_num_instrs(&ir);

  char *name = strtok(passes, ",");
  while (name) {
    if (!strcmp(name, "lse")) {
      lse_run(&ir);
    } else if (!strcmp(name, "cve")) {
      cve_run(&ir);
    } else if (!strcmp(name, "dce")) {
      dce_run(&ir);
    } else if (!strcmp(name, "esimp")) {
      esimp_run(&ir);
    } else if (!strcmp(name, "ra")) {
      ra_run(&ir, x64_registers, x64_num_registers);
    } else {
      LOG_WARNING("Unknown pass %s", name);
    }

    /* print ir after each pass if requested */
    if (!disable_dumps && OPTION_print_after_all) {
      LOG_INFO("===-----------------------------------------------------===");
      LOG_INFO("IR after %s", name);
      LOG_INFO("===-----------------------------------------------------===");
      ir_write(&ir, stdout);
      LOG_INFO("");
    }

    name = strtok(NULL, ",");
  }

  int num_instrs_after = get_num_instrs(&ir);

  /* print out the final ir */
  if (!disable_dumps && !OPTION_print_after_all) {
    ir_write(&ir, stdout);
  }

  /* assemble backend code */
  int host_size = 0;
  int host_num_instrs = 0;
  uint8_t *host_code = NULL;

  jit->backend->reset(jit->backend);
  host_code = jit->backend->assemble_code(jit->backend, &ir, &host_size);

  if (!disable_dumps) {
    LOG_INFO("===-----------------------------------------------------===");
    LOG_INFO("X64 code");
    LOG_INFO("===-----------------------------------------------------===");
    jit->backend->disassemble_code(jit->backend, host_code, host_size, 1,
                                   &host_num_instrs);
  } else {
    jit->backend->disassemble_code(jit->backend, host_code, host_size, 0,
                                   &host_num_instrs);
  }

  /* update stats */
  STAT_guest_instrs += guest_num_instrs;
  STAT_host_instrs += host_num_instrs;
  STAT_ir_instrs += num_instrs_before;
  STAT_ir_instrs_removed += num_instrs_before - num_instrs_after;
}

static void process_dir(struct jit *jit, const char *path) {
  DIR *dir = opendir(path);

  if (!dir) {
    LOG_WARNING("Failed to open directory %s", path);
    return;
  }

  struct dirent *ent = NULL;

  while ((ent = readdir(dir)) != NULL) {
    if (!(ent->d_type & DT_REG)) {
      continue;
    }

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "%s", path,
             ent->d_name);

    LOG_INFO("Processing %s", filename);

    process_file(jit, filename, 1);
  }

  closedir(dir);
}

int main(int argc, char **argv) {
  options_parse(&argc, &argv);

  if (OPTION_help) {
    options_print_help();
    return EXIT_SUCCESS;
  }

  const char *path = argv[1];

  struct jit *jit = jit_create("recc");
  struct jit_guest guest = {0};
  struct jit_backend *backend =
      x64_backend_create(jit, code, code_size, stack_size);
  CHECK(jit_init(jit, &guest, NULL, backend));

  if (fs_isfile(path)) {
    process_file(jit, path, 0);
  } else {
    process_dir(jit, path);
  }

  if (OPTION_stats) {
    pass_stat_print_all();
  }

  jit_destroy(jit);
  x64_backend_destroy(backend);

  return EXIT_SUCCESS;
}
