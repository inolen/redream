#include <inttypes.h>
#include "jit/ir/ir.h"
#include "core/string.h"

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
    case VALUE_LABEL:
      fprintf(output, "lbl");
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
      case VALUE_LABEL: {
        fprintf(output, ".%s", value->str);
      } break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    fprintf(output, "%%%d", (int)value->def->tag);
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
  fprintf(output, "[tag %" PRId64 ", reg %d]", instr->tag, instr->reg);
#endif

  fprintf(output, "\n");
}

static void ir_assign_slots(struct ir *ir) {
  int next_slot = 0;

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    /* don't assign a slot to instructions without a return value */
    if (!instr->result) {
      continue;
    }

    instr->tag = next_slot++;
  }
}

void ir_write(struct ir *ir, FILE *output) {
  ir_assign_slots(ir);

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    ir_write_instr(instr, output);
  }
}
