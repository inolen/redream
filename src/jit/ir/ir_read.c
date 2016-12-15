#include "jit/ir/ir.h"
#include "core/string.h"

enum ir_token {
  TOK_EOF,
  TOK_EOL,
  TOK_COMMA,
  TOK_OPERATOR,
  TOK_TYPE,
  TOK_INTEGER,
  TOK_IDENTIFIER,
};

struct ir_lexeme {
  char s[128];
  uint64_t i;
  enum ir_type ty;
};

struct ir_parser {
  FILE *input;
  enum ir_token tok;
  struct ir_lexeme val;
};

static const char *typenames[] = {"",    "i8",  "i16",  "i32", "i64",
                                  "f32", "f64", "v128", "lbl"};
static const int num_typenames = sizeof(typenames) / sizeof(typenames[0]);

static char ir_lex_get(struct ir_parser *p) {
  return fgetc(p->input);
}

static void ir_lex_unget(struct ir_parser *p, char c) {
  ungetc(c, p->input);
}

static void ir_lex_next(struct ir_parser *p) {
  /* skip past whitespace characters, except newlines */
  char next;
  do {
    next = ir_lex_get(p);
  } while (isspace(next) && next != '\n');

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

  /* test for comma */
  if (next == ',') {
    strncpy(p->val.s, ",", sizeof(p->val.s));
    p->tok = TOK_COMMA;
    return;
  }

  /* test for assignment operator */
  if (next == '=') {
    strncpy(p->val.s, "=", sizeof(p->val.s));
    p->tok = TOK_OPERATOR;
    return;
  }

  /* test for type keyword */
  for (int i = 1; i < num_typenames; i++) {
    const char *typename = typenames[i];
    const char *ptr = typename;
    char tmp = next;

    /* try to match */
    while (*ptr && *ptr == tmp) {
      tmp = ir_lex_get(p);
      ptr++;
    }

    /* if the typename matched, return */
    if (!*ptr) {
      strncpy(p->val.s, typename, sizeof(p->val.s));
      p->val.ty = i;
      p->tok = TOK_TYPE;
      return;
    }

    /* no match, unget everything */
    if (*ptr && ptr != typename) {
      ir_lex_unget(p, tmp);
      ptr--;
    }

    while (*ptr && ptr != typename) {
      ir_lex_unget(p, *ptr);
      ptr--;
    }
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

  /* treat anything else as an identifier */
  char *ptr = p->val.s;
  while (isalpha(next) || isdigit(next) || next == '%' || next == '.' ||
         next == '_') {
    *ptr++ = next;
    next = ir_lex_get(p);
  }
  ir_lex_unget(p, next);
  *ptr = 0;

  p->tok = TOK_IDENTIFIER;
  return;
}

int ir_parse_type(struct ir_parser *p, struct ir *ir, enum ir_type *type) {
  if (p->tok != TOK_TYPE) {
    LOG_INFO("Unexpected token %d when parsing type", p->tok);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  *type = p->val.ty;

  return 1;
}

int ir_parse_op(struct ir_parser *p, struct ir *ir, enum ir_op *op) {
  if (p->tok != TOK_IDENTIFIER) {
    LOG_INFO("Unexpected token %d when parsing op", p->tok);
    return 0;
  }

  const char *op_str = p->val.s;

  /* match token against opnames */
  int i;
  for (i = 0; i < NUM_OPS; i++) {
    if (!strcasecmp(op_str, ir_op_names[i])) {
      break;
    }
  }

  if (i == NUM_OPS) {
    LOG_INFO("Unexpected op '%s'", op_str);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  *op = (enum ir_op)i;

  return 1;
}

int ir_parse_value(struct ir_parser *p, struct ir *ir,
                   struct ir_value **value) {
  /* parse value type */
  enum ir_type type;
  if (!ir_parse_type(p, ir, &type)) {
    return 0;
  }

  /* parse value */
  if (p->tok == TOK_IDENTIFIER) {
    const char *ident = p->val.s;

    if (ident[0] == '%') {
      /* slot reference, lookup the result for the instruction in that slot */
      int slot = atoi(&ident[1]);

      struct ir_instr *instr =
          list_first_entry(&ir->instrs, struct ir_instr, it);
      while (instr) {
        if (instr->tag == slot) {
          break;
        }
        instr = list_next_entry(instr, struct ir_instr, it);
      }
      CHECK_NOTNULL(instr);

      *value = instr->result;
    } else if (ident[0] == '.') {
      /* label reference */
      const char *label = &ident[1];

      *value = ir_alloc_label(ir, "%s", label);
    }
  } else if (p->tok == TOK_INTEGER) {
    switch (type) {
      case VALUE_I8: {
        uint8_t v = (uint8_t)p->val.i;
        *value = ir_alloc_i8(ir, v);
      } break;
      case VALUE_I16: {
        uint16_t v = (uint16_t)p->val.i;
        *value = ir_alloc_i16(ir, v);
      } break;
      case VALUE_I32: {
        uint32_t v = (uint32_t)p->val.i;
        *value = ir_alloc_i32(ir, v);
      } break;
      case VALUE_I64: {
        uint64_t v = (uint64_t)p->val.i;
        *value = ir_alloc_i64(ir, v);
      } break;
      case VALUE_F32: {
        uint32_t v = (uint32_t)p->val.i;
        *value = ir_alloc_f32(ir, *(float *)&v);
      } break;
      case VALUE_F64: {
        uint64_t v = (uint64_t)p->val.i;
        *value = ir_alloc_f64(ir, *(double *)&v);
      } break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  return 1;
}

int ir_parse_operator(struct ir_parser *p, struct ir *ir) {
  const char *op_str = p->val.s;

  if (strcmp(op_str, "=")) {
    LOG_INFO("Unexpected operator '%s'", op_str);
    return 0;
  }

  /* eat token */
  ir_lex_next(p);

  /* nothing to do, there's only one operator token */

  return 1;
}

int ir_parse_instr(struct ir_parser *p, struct ir *ir) {
  int slot = -1;
  enum ir_type type = VALUE_V;
  struct ir_value *arg[3] = {0};

  /* parse result type and slot number */
  if (p->tok == TOK_TYPE) {
    if (!ir_parse_type(p, ir, &type)) {
      return 0;
    }

    const char *ident = p->val.s;
    if (ident[0] != '%') {
      return 0;
    }
    slot = atoi(&ident[1]);
    ir_lex_next(p);

    if (!ir_parse_operator(p, ir)) {
      return 0;
    }
  }

  /* parse op */
  enum ir_op op;
  if (!ir_parse_op(p, ir, &op)) {
    return 0;
  }

  /* parse arguments */
  if (p->tok == TOK_TYPE) {
    for (int i = 0; i < MAX_INSTR_ARGS; i++) {
      if (!ir_parse_value(p, ir, &arg[i])) {
        return 0;
      }

      if (p->tok != TOK_COMMA) {
        break;
      }

      /* eat comma and move onto the next argument */
      ir_lex_next(p);
    }
  }

  /* create instruction */
  struct ir_instr *instr = ir_append_instr(ir, op, type);

  for (int i = 0; i < MAX_INSTR_ARGS; i++) {
    ir_set_arg(ir, instr, i, arg[i]);
  }

  instr->tag = slot;

  return 1;
}

int ir_read(FILE *input, struct ir *ir) {
  struct ir_parser p = {0};
  p.input = input;

  while (1) {
    ir_lex_next(&p);

    if (p.tok == TOK_EOF) {
      break;
    }

    if (!ir_parse_instr(&p, ir)) {
      return 0;
    }
  }

  return 1;
}
