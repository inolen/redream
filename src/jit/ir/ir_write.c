#include <inttypes.h>
#include "core/string.h"
#include "jit/ir/ir.h"

static void ir_write_type(enum ir_type type, FILE *output) {
  switch (type) {
    case VALUE_I8:
      fprintf(output, "i8");
      break;
    case VALUE_I16:
      fprintf(output, "i16");
      break;
    case VALUE_I32:
      fprintf(output, "i32");
      break;
    case VALUE_I64:
      fprintf(output, "i64");
      break;
    case VALUE_F32:
      fprintf(output, "f32");
      break;
    case VALUE_F64:
      fprintf(output, "f64");
      break;
    case VALUE_V128:
      fprintf(output, "v128");
      break;
    case VALUE_STRING:
      fprintf(output, "str");
      break;
    case VALUE_BLOCK:
      fprintf(output, "blk");
      break;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

static void ir_write_op(enum ir_op op, FILE *output) {
  const char *name = ir_op_names[op];

  while (*name) {
    fprintf(output, "%c", tolower(*name));
    name++;
  }
}

static void ir_write_value(const struct ir_value *value, FILE *output) {
  ir_write_type(value->type, output);

  fprintf(output, " ");

  if (ir_is_constant(value)) {
    switch (value->type) {
      case VALUE_I8:
        /* force to int to avoid printing out as a character */
        fprintf(output, "0x%x", value->i8);
        break;
      case VALUE_I16:
        fprintf(output, "0x%x", value->i16);
        break;
      case VALUE_I32:
        fprintf(output, "0x%x", value->i32);
        break;
      case VALUE_I64:
        fprintf(output, "0x%" PRIx64, value->i64);
        break;
      case VALUE_F32: {
        float v = value->f32;
        fprintf(output, "0x%x", *(uint32_t *)&v);
      } break;
      case VALUE_F64: {
        double v = value->f64;
        fprintf(output, "0x%" PRIx64, *(uint64_t *)&v);
      } break;
      case VALUE_STRING: {
        fprintf(output, "'%s'", value->str);
      } break;
      case VALUE_BLOCK:
        fprintf(output, "%%%s", value->blk->label);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    fprintf(output, "%%%s", value->def->label);
  }
}

static void ir_write_instr(const struct ir_instr *instr, FILE *output) {
  /* print result value if it exists */
  if (instr->result) {
    ir_write_value(instr->result, output);
    fprintf(output, " = ");
  }

  /* print the actual op */
  ir_write_op(instr->op, output);
  fprintf(output, " ");

  /* print each argument */
  int need_comma = 0;

  for (int i = 0; i < 3; i++) {
    const struct ir_value *arg = instr->arg[i];

    if (!arg) {
      continue;
    }

    if (need_comma) {
      fprintf(output, ", ");
      need_comma = 0;
    }

    ir_write_value(arg, output);

    need_comma = 1;
  }

#if 0
  fprintf(output, " [tag %" PRId64 ", reg %d]", instr->tag, instr->result ? instr->result->reg : -1);
#endif

  fprintf(output, "\n");
}

static void ir_write_block(const struct ir_block *block, FILE *output) {
  fprintf(output, "%%%s:\n", block->label);

  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    ir_write_instr(instr, output);
  }

  fprintf(output, "\n");
}

static void ir_assign_default_labels(struct ir *ir) {
  int id = 0;

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    if (!block->label) {
      ir_set_block_label(ir, block, "%d", id++);
    }

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      if (!instr->label) {
        ir_set_instr_label(ir, instr, "%d", id++);
      }
    }
  }
}

void ir_write(struct ir *ir, FILE *output) {
  ir_assign_default_labels(ir);

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    ir_write_block(block, output);
  }
}
