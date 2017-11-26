#include "core/core.h"
#include "jit/ir/ir.h"

struct ir_writer {
  struct ir *ir;
  int *labels;
};

static void ir_destroy_writer(struct ir_writer *w) {
  free(w->labels);
}

static void ir_insert_block_label(struct ir_writer *w,
                                  const struct ir_block *block, int label) {
  w->labels[(uint8_t *)block - w->ir->buffer] = label;
}

static void ir_insert_instr_label(struct ir_writer *w,
                                  const struct ir_instr *instr, int label) {
  w->labels[(uint8_t *)instr - w->ir->buffer] = label;
}

static int ir_get_block_label(struct ir_writer *w,
                              const struct ir_block *block) {
  return w->labels[(uint8_t *)block - w->ir->buffer];
}

static int ir_get_instr_label(struct ir_writer *w,
                              const struct ir_instr *instr) {
  return w->labels[(uint8_t *)instr - w->ir->buffer];
}

static void ir_write_type(struct ir_writer *w, enum ir_type type,
                          FILE *output) {
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
    case VALUE_BLOCK:
      fprintf(output, "blk");
      break;
    default:
      LOG_FATAL("unexpected value type");
      break;
  }
}

static void ir_write_op(struct ir_writer *w, enum ir_op op, FILE *output) {
  const struct ir_opdef *def = &ir_opdefs[op];
  const char *name = def->name;

  while (*name) {
    fprintf(output, "%c", tolower(*name));
    name++;
  }
}

static void ir_write_value(struct ir_writer *w, const struct ir_value *value,
                           FILE *output) {
  ir_write_type(w, value->type, output);

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
      case VALUE_BLOCK:
        fprintf(output, "%%%d", ir_get_block_label(w, value->blk));
        break;
      default:
        LOG_FATAL("unexpected value type");
        break;
    }
  } else {
    fprintf(output, "%%%d", ir_get_instr_label(w, value->def));
  }
}

static void ir_write_meta(struct ir_writer *w, const void *obj, FILE *output) {
  int need_exclamation = 1;
  int need_comma = 0;

  for (int kind = 0; kind < IR_NUM_META; kind++) {
    struct ir_value *value = ir_get_meta(w->ir, obj, kind);

    if (!value) {
      continue;
    }

    if (need_exclamation) {
      fprintf(output, " !");
      need_exclamation = 0;
    }

    if (need_comma) {
      fprintf(output, ", ");
      need_comma = 0;
    }

    fprintf(output, "%s ", ir_meta_names[kind]);

    ir_write_value(w, value, output);

    need_comma = 1;
  }
}

static void ir_write_instr(struct ir_writer *w, const struct ir_instr *instr,
                           FILE *output) {
  /* print result value if it exists */
  if (instr->result) {
    ir_write_value(w, instr->result, output);
    fprintf(output, " = ");
  }

  /* print the actual op */
  ir_write_op(w, instr->op, output);

  /* print each argument */
  int need_space = 1;
  int need_comma = 0;

  for (int i = 0; i < IR_MAX_ARGS; i++) {
    const struct ir_value *arg = instr->arg[i];

    if (!arg) {
      continue;
    }

    if (need_space) {
      fprintf(output, " ");
      need_space = 0;
    }

    if (need_comma) {
      fprintf(output, ", ");
      need_comma = 0;
    }

    ir_write_value(w, arg, output);

    need_comma = 1;
  }

  ir_write_meta(w, instr, output);

#if 0
  fprintf(output, "\t# tag=%" PRId64 " reg=%d", instr->tag,
          instr->result ? instr->result->reg : -1);
#endif

  fprintf(output, "\n");
}

static void ir_write_block(struct ir_writer *w, const struct ir_block *block,
                           FILE *output) {
  /* write out control flow information */
  fprintf(output, "# predecessors ");
  list_for_each_entry(edge, &block->incoming, struct ir_edge, it) {
    fprintf(output, "%%%d ", ir_get_block_label(w, edge->src));
  }
  fprintf(output, "\n");

  fprintf(output, "# successors ");
  list_for_each_entry(edge, &block->outgoing, struct ir_edge, it) {
    fprintf(output, "%%%d ", ir_get_block_label(w, edge->dst));
  }
  fprintf(output, "\n");

  /* write out actual block */
  fprintf(output, "%%%d:", ir_get_block_label(w, block));
  ir_write_meta(w, block, output);
  fprintf(output, "\n");

  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    ir_write_instr(w, instr, output);
  }
}

static void ir_assign_labels(struct ir_writer *w) {
  int label = 0;

  w->labels = malloc(sizeof(int) * w->ir->capacity);

  list_for_each_entry(block, &w->ir->blocks, struct ir_block, it) {
    ir_insert_block_label(w, block, label++);

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      ir_insert_instr_label(w, instr, label++);
    }
  }
}

void ir_write(struct ir *ir, FILE *output) {
  struct ir_writer w = {0};
  w.ir = ir;

  ir_assign_labels(&w);

  fprintf(output, "#==--------------------------------------------------==#\n");
  fprintf(output, "# ir\n");
  fprintf(output, "#==--------------------------------------------------==#\n");

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    ir_write_block(&w, block, output);
  }

  ir_destroy_writer(&w);
}
