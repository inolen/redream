#include "core/core.h"
#include "jit/ir/ir.h"

enum ir_token {
  TOK_EOF,
  TOK_EOL,
  TOK_OPERATOR,
  TOK_INTEGER,
  TOK_STRING,
  TOK_IDENTIFIER,
  TOK_TYPE,
  TOK_OP,
};

struct ir_reference {
  struct ir_instr *instr;
  int arg;
  enum ir_type type;
  int label;
  struct list_node it;
};

struct ir_lexeme {
  char s[128];
  uint64_t i;
  enum ir_op op;
  enum ir_type ty;
};

struct ir_parser {
  FILE *input;
  struct ir *ir;

  enum ir_token tok;
  struct ir_lexeme val;
  struct list refs;

  int *labels;
};

static const char *typenames[] = {"",    "i8",  "i16",  "i32", "i64",
                                  "f32", "f64", "v128", "blk"};
static const int num_typenames = sizeof(typenames) / sizeof(typenames[0]);

static int ir_lex_get(struct ir_parser *p) {
  return fgetc(p->input);
}

static void ir_lex_unget(struct ir_parser *p, int c) {
  ungetc(c, p->input);
}

static void ir_lex_next(struct ir_parser *p) {
  /* skip past whitespace characters, except newlines */
  int next;
  do {
    next = ir_lex_get(p);
  } while (isspace(next) && next != '\n');

  /* ignore comment lines */
  while (next == '#') {
    while (next != '\n') {
      next = ir_lex_get(p);
    }
    next = ir_lex_get(p);
  }

  /* test for end of file */
  if (next == EOF) {
    strncpy(p->val.s, "", sizeof(p->val.s));
    p->tok = TOK_EOF;
    return;
  }

  /* test for newline */
  if (next == '\n') {
    strncpy(p->val.s, "\n", sizeof(p->val.s));

    /* chomp adjacent newlines */
    while (next == '\n') {
      next = ir_lex_get(p);
    }
    ir_lex_unget(p, next);

    p->tok = TOK_EOL;
    return;
  }

  /* test for assignment operator */
  if (next == ':' || next == ',' || next == '=' || next == '!') {
    snprintf(p->val.s, sizeof(p->val.s), "%c", next);
    p->tok = TOK_OPERATOR;
    return;
  }

  /* test for hex literal */
  if (next == '0') {
    next = ir_lex_get(p);

    if (next == 'x') {
      next = ir_lex_get(p);

      /* parse literal */
      p->val.i = 0;
      while (isxdigit(next)) {
        p->val.i <<= 4;
        p->val.i |= xtoi(next);
        next = ir_lex_get(p);
      }
      ir_lex_unget(p, next);

      p->tok = TOK_INTEGER;
      return;
    } else {
      ir_lex_unget(p, next);
    }
  }

  /* test for string literal */
  if (next == '\'') {
    next = ir_lex_get(p);

    char *ptr = p->val.s;
    while (next != '\'') {
      *ptr++ = (char)next;
      next = ir_lex_get(p);
    }
    *ptr = 0;

    p->tok = TOK_STRING;
    return;
  }

  /* treat anything else as an identifier */
  char *ptr = p->val.s;
  while (isalpha(next) || isdigit(next) || next == '%' || next == '_') {
    *ptr++ = (char)next;
    next = ir_lex_get(p);
  }
  ir_lex_unget(p, next);
  *ptr = 0;

  p->tok = TOK_IDENTIFIER;

  /* test for type keyword */
  for (int i = 1; i < num_typenames; i++) {
    const char *typename = typenames[i];

    if (!strcasecmp(p->val.s, typename)) {
      p->val.ty = i;
      p->tok = TOK_TYPE;
      return;
    }
  }

  /* test for op keyword */
  for (int i = 0; i < IR_NUM_OPS; i++) {
    const struct ir_opdef *def = &ir_opdefs[i];

    if (!strcasecmp(p->val.s, def->name)) {
      p->val.op = i;
      p->tok = TOK_OP;
      return;
    }
  }
}

static void ir_destroy_parser(struct ir_parser *p) {
  free(p->labels);

  list_for_each_entry_safe(ref, &p->refs, struct ir_reference, it) {
    free(ref);
  }
}

static void ir_insert_block_label(struct ir_parser *p,
                                  const struct ir_block *block, int label) {
  p->labels[(uint8_t *)block - p->ir->buffer] = label;
}

static void ir_insert_instr_label(struct ir_parser *p,
                                  const struct ir_instr *instr, int label) {
  p->labels[(uint8_t *)instr - p->ir->buffer] = label;
}

static int ir_get_block_label(struct ir_parser *p,
                              const struct ir_block *block) {
  return p->labels[(uint8_t *)block - p->ir->buffer];
}

static int ir_get_instr_label(struct ir_parser *p,
                              const struct ir_instr *instr) {
  return p->labels[(uint8_t *)instr - p->ir->buffer];
}

static int ir_resolve_references(struct ir_parser *p) {
  list_for_each_entry(ref, &p->refs, struct ir_reference, it) {
    struct ir_value *value = NULL;

    if (ref->type == VALUE_BLOCK) {
      struct ir_block *found = NULL;

      list_for_each_entry(block, &p->ir->blocks, struct ir_block, it) {
        if (ir_get_block_label(p, block) == ref->label) {
          found = block;
          break;
        }
      }

      if (!found) {
        LOG_INFO("failed to resolve reference for %%%d", ref->label);
        return 0;
      }

      value = ir_alloc_block_ref(p->ir, found);
    } else {
      struct ir_instr *found = NULL;
      list_for_each_entry(block, &p->ir->blocks, struct ir_block, it) {
        if (found) {
          break;
        }

        list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
          if (ir_get_instr_label(p, instr) == ref->label) {
            found = instr;
            break;
          }
        }
      }

      if (!found) {
        LOG_INFO("failed to resolve reference for %%%d", ref->label);
        return 0;
      }

      value = found->result;
    }

    ir_set_arg(p->ir, ref->instr, ref->arg, value);
  }

  return 1;
}

static void ir_defer_reference(struct ir_parser *p, struct ir_instr *instr,
                               int arg, enum ir_type type, int label) {
  struct ir_reference *ref = calloc(1, sizeof(struct ir_reference));
  ref->instr = instr;
  ref->arg = arg;
  ref->type = type;
  ref->label = label;
  list_add(&p->refs, &ref->it);
}

static int ir_parse_type(struct ir_parser *p, enum ir_type *type) {
  if (p->tok != TOK_TYPE) {
    LOG_INFO("unexpected token %d when parsing type", p->tok);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  *type = p->val.ty;

  return 1;
}

static int ir_parse_op(struct ir_parser *p, enum ir_op *op) {
  if (p->tok != TOK_OP) {
    LOG_INFO("unexpected token %d when parsing op", p->tok);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  *op = p->val.op;

  return 1;
}

static int ir_parse_operator(struct ir_parser *p) {
  const char *op_str = p->val.s;

  if (strcmp(op_str, "=")) {
    LOG_INFO("unexpected operator '%s'", op_str);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  /* nothing to do, there's only one operator token */

  return 1;
}

static int ir_parse_label(struct ir_parser *p, int *label) {
  if (p->tok != TOK_IDENTIFIER) {
    LOG_INFO("unexpected token %d when parsing label", p->tok);
    return 0;
  }

  const char *ident = p->val.s;
  if (ident[0] != '%') {
    LOG_INFO("expected label '%s' to begin with %%", ident);
    return 0;
  }

  *label = strtol(&ident[1], NULL, 10);
  ir_lex_next(p);

  return 1;
}

static int ir_parse_constant(struct ir_parser *p, enum ir_type type,
                             struct ir_value **value) {
  if (p->tok != TOK_INTEGER) {
    LOG_INFO("unexpected token %d when parsing constant", p->tok);
    return 0;
  }

  switch (type) {
    case VALUE_I8: {
      uint8_t v = (uint8_t)p->val.i;
      *value = ir_alloc_i8(p->ir, v);
    } break;
    case VALUE_I16: {
      uint16_t v = (uint16_t)p->val.i;
      *value = ir_alloc_i16(p->ir, v);
    } break;
    case VALUE_I32: {
      uint32_t v = (uint32_t)p->val.i;
      *value = ir_alloc_i32(p->ir, v);
    } break;
    case VALUE_I64: {
      uint64_t v = (uint64_t)p->val.i;
      *value = ir_alloc_i64(p->ir, v);
    } break;
    case VALUE_F32: {
      uint32_t v = (uint32_t)p->val.i;
      *value = ir_alloc_f32(p->ir, *(float *)&v);
    } break;
    case VALUE_F64: {
      uint64_t v = (uint64_t)p->val.i;
      *value = ir_alloc_f64(p->ir, *(double *)&v);
    } break;
    default:
      LOG_FATAL("unexpected value type");
      break;
  }

  /* eat token */
  ir_lex_next(p);

  return 1;
}

static int ir_parse_arg(struct ir_parser *p, struct ir_instr *instr, int arg) {
  /* parse value type */
  enum ir_type type;
  if (!ir_parse_type(p, &type)) {
    return 0;
  }

  /* parse value */
  if (p->tok == TOK_IDENTIFIER) {
    const char *ident = p->val.s;

    if (ident[0] != '%') {
      LOG_INFO("expected identifier to begin with %%");
      return 0;
    }

    /* label reference, defer resolution until after all blocks / values have
       been parsed */
    int label = strtol(&ident[1], NULL, 10);
    ir_defer_reference(p, instr, arg, type, label);

    /* eat token */
    ir_lex_next(p);
  } else {
    struct ir_value *value = NULL;
    if (!ir_parse_constant(p, type, &value)) {
      return 0;
    }

    ir_set_arg(p->ir, instr, arg, value);
  }

  return 1;
}

static int ir_parse_meta(struct ir_parser *p, void *obj) {
  if (p->tok != TOK_OPERATOR || p->val.s[0] != '!') {
    /* meta data is optional */
    return 1;
  }
  ir_lex_next(p);

  while (p->tok == TOK_IDENTIFIER) {
    for (int kind = 0; kind < IR_NUM_META; kind++) {
      if (strcasecmp(p->val.s, ir_meta_names[kind])) {
        continue;
      }

      /* eat name */
      ir_lex_next(p);

      /* parse value type */
      enum ir_type type;
      if (!ir_parse_type(p, &type)) {
        return 0;
      }

      /* parse value */
      struct ir_value *value = NULL;
      if (!ir_parse_constant(p, type, &value)) {
        return 0;
      }

      /* attach meta data to object */
      ir_set_meta(p->ir, obj, kind, value);

      /* break if no comma */
      if (p->tok != TOK_OPERATOR) {
        break;
      }

      /* eat comma and move onto the next argument */
      ir_lex_next(p);
    }
  }

  return 1;
}

static int ir_parse_instr(struct ir_parser *p) {
  enum ir_type type = VALUE_V;
  int label = 0;

  /* parse result type and label */
  if (p->tok == TOK_TYPE) {
    if (!ir_parse_type(p, &type)) {
      return 0;
    }

    if (!ir_parse_label(p, &label)) {
      return 0;
    }

    if (!ir_parse_operator(p)) {
      return 0;
    }
  }

  /* parse op */
  enum ir_op op;
  if (!ir_parse_op(p, &op)) {
    return 0;
  }

  /* create instruction */
  struct ir_instr *instr = ir_append_instr(p->ir, op, type);

  /* parse arguments */
  if (p->tok == TOK_TYPE) {
    for (int i = 0; i < IR_MAX_ARGS; i++) {
      if (!ir_parse_arg(p, instr, i)) {
        return 0;
      }

      /* break if no comma */
      if (p->tok != TOK_OPERATOR) {
        break;
      }

      /* eat comma and move onto the next argument */
      ir_lex_next(p);
    }
  }

  if (!ir_parse_meta(p, instr)) {
    return 0;
  }

  ir_insert_instr_label(p, instr, label);

  return 1;
}

static int ir_parse_block(struct ir_parser *p) {
  if (p->tok != TOK_IDENTIFIER) {
    LOG_INFO("unexpected token %d when parsing block", p->tok);
    return 0;
  }

  int label;
  if (!ir_parse_label(p, &label)) {
    return 0;
  }

  if (p->tok != TOK_OPERATOR || p->val.s[0] != ':') {
    LOG_INFO("expected label to be followed by : operator");
    return 0;
  }
  ir_lex_next(p);

  struct ir_block *block = ir_append_block(p->ir);
  ir_insert_block_label(p, block, label);
  ir_set_current_block(p->ir, block);

  if (!ir_parse_meta(p, block)) {
    return 0;
  }

  return 1;
}

int ir_read(FILE *input, struct ir *ir) {
  struct ir_parser p = {0};
  p.input = input;
  p.ir = ir;
  p.labels = malloc(sizeof(int) * ir->capacity);

  int res = 1;

  while (1) {
    ir_lex_next(&p);

    if (p.tok == TOK_EOL) {
      continue;
    }

    if (p.tok == TOK_EOF) {
      if (!ir_resolve_references(&p)) {
        res = 0;
      }
      break;
    }

    if (p.tok == TOK_IDENTIFIER) {
      if (!ir_parse_block(&p)) {
        res = 0;
        break;
      }
    } else {
      if (!ir_parse_instr(&p)) {
        res = 0;
        break;
      }
    }
  }

  ir_destroy_parser(&p);

  return res;
}
