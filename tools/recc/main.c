#include "core/core.h"
#include "core/filesystem.h"
#include "core/option.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_guest.h"
#include "jit/pass_stats.h"
#include "jit/passes/constant_propagation_pass.h"
#include "jit/passes/control_flow_analysis_pass.h"
#include "jit/passes/dead_code_elimination_pass.h"
#include "jit/passes/expression_simplification_pass.h"
#include "jit/passes/load_store_elimination_pass.h"
#include "jit/passes/register_allocation_pass.h"

DEFINE_OPTION_STRING(pass, "cfa,lse,cprop,esimp,dce,ra",
                     "Comma-separated list of passes to run");

DEFINE_PASS_STAT(ir_instrs_total, "total ir instructions");
DEFINE_PASS_STAT(ir_instrs_removed, "removed ir instructions");

DEFINE_JIT_CODE_BUFFER(code);
static uint8_t ir_buffer[1024 * 1024];

static int get_num_instrs(const struct ir *ir) {
  int n = 0;

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      ((void)instr);
      n++;
    }
  }

  return n;
}

static void sanitize_ir(struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      if (instr->op != OP_CALL && instr->op != OP_FALLBACK) {
        continue;
      }

      /* ensure that address are within 2 GB of the code buffer */
      uint64_t addr = instr->arg[0]->i64;
      addr = (uint64_t)code | (addr & 0x7fffffff);
      ir_set_arg0(ir, instr, ir_alloc_i64(ir, addr));
    }
  }
}

static void process_file(struct jit_backend *backend, const char *filename,
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

  /* sanitize absolute addresses in the ir */
  sanitize_ir(&ir);

  /* run optimization passes */
  char passes[OPTION_MAX_LENGTH];
  strncpy(passes, OPTION_pass, sizeof(passes));

  int num_instrs_before = get_num_instrs(&ir);

  char *name = strtok(passes, ",");
  while (name) {
    if (!strcmp(name, "cfa")) {
      struct cfa *cfa = cfa_create();
      cfa_run(cfa, &ir);
      cfa_destroy(cfa);
    } else if (!strcmp(name, "lse")) {
      struct lse *lse = lse_create();
      lse_run(lse, &ir);
      lse_destroy(lse);
    } else if (!strcmp(name, "cprop")) {
      struct cprop *cprop = cprop_create();
      cprop_run(cprop, &ir);
      cprop_destroy(cprop);
    } else if (!strcmp(name, "dce")) {
      struct dce *dce = dce_create();
      dce_run(dce, &ir);
      dce_destroy(dce);
    } else if (!strcmp(name, "esimp")) {
      struct esimp *esimp = esimp_create();
      esimp_run(esimp, &ir);
      esimp_destroy(esimp);
    } else if (!strcmp(name, "ra")) {
      struct ra *ra = ra_create(backend->registers, backend->num_registers,
                                backend->emitters, backend->num_emitters);
      ra_run(ra, &ir);
      ra_destroy(ra);
    } else {
      LOG_WARNING("unknown pass %s", name);
    }

    /* print ir after each pass if requested */
    if (!disable_dumps) {
      LOG_INFO("===-----------------------------------------------------===");
      LOG_INFO("ir after %s", name);
      LOG_INFO("===-----------------------------------------------------===");
      ir_write(&ir, stdout);
      LOG_INFO("");
    }

    name = strtok(NULL, ",");
  }

  int num_instrs_after = get_num_instrs(&ir);

  /* assemble backend code */
  backend->reset(backend);
  uint8_t *host_addr = NULL;
  int host_size = 0;
  int res =
      backend->assemble_code(backend, &ir, &host_addr, &host_size, NULL, NULL);
  CHECK(res);

  if (!disable_dumps) {
    LOG_INFO("===-----------------------------------------------------===");
    LOG_INFO("x64 code");
    LOG_INFO("===-----------------------------------------------------===");
    backend->dump_code(backend, host_addr, host_size, stdout);
    LOG_INFO("");
  }

  /* update stats */
  STAT_ir_instrs_total += num_instrs_before;
  STAT_ir_instrs_removed += num_instrs_before - num_instrs_after;
}

static void process_dir(struct jit_backend *backend, const char *path) {
  DIR *dir = opendir(path);

  if (!dir) {
    LOG_WARNING("failed to open directory %s", path);
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

    LOG_INFO("processing %s", filename);

    process_file(backend, filename, 1);
  }

  closedir(dir);
}

int main(int argc, char **argv) {
  if (!options_parse(&argc, &argv)) {
    return EXIT_FAILURE;
  }

  const char *path = argv[1];

  struct jit_guest guest = {0};
  guest.addr_mask = 0xff;

  struct jit_backend *backend = x64_backend_create(&guest, code, sizeof(code));

  if (fs_isfile(path)) {
    process_file(backend, path, 0);
  } else {
    process_dir(backend, path);
  }

  LOG_INFO("");
  pass_stats_dump();

  backend->destroy(backend);

  return EXIT_SUCCESS;
}
