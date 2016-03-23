#include "core/string.h"
#include "jit/ir/ir_reader.h"

using namespace re;
using namespace re::jit;
using namespace re::jit::ir;

struct IRType {
  const char *name;
  ValueType ty;
};

static IRType s_ir_types[] = {
    {"i8", VALUE_I8},   {"i16", VALUE_I16}, {"i32", VALUE_I32},
    {"i64", VALUE_I64}, {"f32", VALUE_F32}, {"f64", VALUE_F64},
};
static const int s_num_ir_types = sizeof(s_ir_types) / sizeof(s_ir_types[0]);

IRLexer::IRLexer(std::istream &input) : input_(input) {}

IRToken IRLexer::Next() {
  // skip past whitespace characters, except newlines
  char next;
  do {
    next = Get();
  } while (isspace(next) && next != '\n');

  // test for end of file
  if (next == EOF) {
    strncpy(val_.s, "", sizeof(val_.s));
    return (tok_ = TOK_EOF);
  }

  // test for newline
  if (next == '\n') {
    strncpy(val_.s, "\n", sizeof(val_.s));

    // chomp adjacent newlines
    while (next == '\n') {
      next = Get();
    }
    Unget();

    return (tok_ = TOK_EOL);
  }

  // test for comma
  if (next == ',') {
    strncpy(val_.s, ",", sizeof(val_.s));
    return (tok_ = TOK_COMMA);
  }

  // test for assignment operator
  if (next == '=') {
    strncpy(val_.s, "=", sizeof(val_.s));
    return (tok_ = TOK_OPERATOR);
  }

  // test for type keyword
  for (int i = 0; i < s_num_ir_types; i++) {
    IRType &ir_type = s_ir_types[i];
    const char *ptr = ir_type.name;
    char tmp = next;

    // try to match
    while (*ptr && *ptr == tmp) {
      tmp = Get();
      ptr++;
    }

    // if we had a match, return
    if (!*ptr) {
      strncpy(val_.s, ir_type.name, sizeof(val_.s));
      val_.ty = ir_type.ty;
      return (tok_ = TOK_TYPE);
    }

    // no match, undo
    while (*ptr && ptr != ir_type.name) {
      Unget();
      ptr--;
    }
  }

  // test for hex literal
  if (next == '0') {
    next = Get();

    if (next == 'x') {
      next = Get();

      // parse literal
      val_.i = 0;
      while (isxdigit(next)) {
        val_.i <<= 4;
        val_.i |= xtoi(next);
        next = Get();
      }
      Unget();

      return (tok_ = TOK_INTEGER);
    } else {
      Unget();
    }
  }

  // treat anything else as an identifier
  char *ptr = val_.s;
  while (isalpha(next) || isdigit(next) || next == '%' || next == '_') {
    *ptr++ = next;
    next = Get();
  }
  Unget();
  *ptr = 0;

  return (tok_ = TOK_IDENTIFIER);
}

char IRLexer::Get() { return input_.get(); }

void IRLexer::Unget() { input_.unget(); }

bool IRReader::Parse(std::istream &input, IRBuilder &builder) {
  IRLexer lex(input);

  while (true) {
    IRToken tok = lex.Next();

    if (tok == TOK_EOF) {
      break;
    }

    if (!ParseInstruction(lex, builder)) {
      return false;
    }
  }

  return true;
}

bool IRReader::ParseType(IRLexer &lex, IRBuilder &builder, ValueType *type) {
  if (lex.tok() != TOK_TYPE) {
    LOG_INFO("Unexpected token %d when parsing type");
    return false;
  }

  // eat token
  lex.Next();

  *type = lex.val().ty;

  return true;
}

bool IRReader::ParseOp(IRLexer &lex, IRBuilder &builder, Op *op) {
  const char *op_str = lex.val().s;

  // match token against opnames
  int i;
  for (i = 0; i < NUM_OPS; i++) {
    if (!strcasecmp(op_str, Opnames[i])) {
      break;
    }
  }

  // eat token
  lex.Next();

  if (i == NUM_OPS) {
    LOG_INFO("Unexpected op '%s'", op_str);
    return false;
  }

  *op = static_cast<Op>(i);

  return true;
}

bool IRReader::ParseValue(IRLexer &lex, IRBuilder &builder, Value **value) {
  // parse value type
  ValueType type;
  if (!ParseType(lex, builder, &type)) {
    return false;
  }

  // parse value
  if (lex.tok() == TOK_IDENTIFIER) {
    const char *ident = lex.val().s;

    if (ident[0] != '%') {
      return false;
    }

    int slot = atoi(&ident[1]);
    auto it = slots_.find(slot);
    CHECK_NE(it, slots_.end());

    *value = it->second;
  } else if (lex.tok() == TOK_INTEGER) {
    switch (type) {
      case VALUE_I8: {
        uint8_t v = static_cast<uint8_t>(lex.val().i);
        *value = builder.AllocConstant(v);
      } break;
      case VALUE_I16: {
        uint16_t v = static_cast<uint16_t>(lex.val().i);
        *value = builder.AllocConstant(v);
      } break;
      case VALUE_I32: {
        uint32_t v = static_cast<uint32_t>(lex.val().i);
        *value = builder.AllocConstant(v);
      } break;
      case VALUE_I64: {
        uint64_t v = static_cast<uint64_t>(lex.val().i);
        *value = builder.AllocConstant(v);
      } break;
      case VALUE_F32: {
        uint32_t v = static_cast<uint32_t>(lex.val().i);
        *value = builder.AllocConstant(*reinterpret_cast<float *>(&v));
      } break;
      case VALUE_F64: {
        uint64_t v = static_cast<uint64_t>(lex.val().i);
        *value = builder.AllocConstant(*reinterpret_cast<double *>(&v));
      } break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    return false;
  }

  // eat token
  lex.Next();

  return true;
}

bool IRReader::ParseOperator(IRLexer &lex, IRBuilder &builder) {
  const char *op_str = lex.val().s;

  if (strcmp(op_str, "=")) {
    LOG_INFO("Unexpected operator '%s'", op_str);
    return false;
  }

  // eat token
  lex.Next();

  // nothing to do, there's only one operator token

  return true;
}

bool IRReader::ParseInstruction(IRLexer &lex, IRBuilder &builder) {
  int slot = -1;
  ValueType type = VALUE_V;
  Value *arg[3] = {};

  // parse result type and slot number
  if (lex.tok() == TOK_TYPE) {
    if (!ParseType(lex, builder, &type)) {
      return false;
    }

    const char *ident = lex.val().s;
    if (ident[0] != '%') {
      return false;
    }
    slot = atoi(&ident[1]);
    lex.Next();

    if (!ParseOperator(lex, builder)) {
      return false;
    }
  }

  // parse op
  Op op;
  if (!ParseOp(lex, builder, &op)) {
    return false;
  }

  // parse arguments
  for (int i = 0; i < 3; i++) {
    ParseValue(lex, builder, &arg[i]);

    if (lex.tok() != TOK_COMMA) {
      break;
    }

    // eat comma and move onto the next argument
    lex.Next();
  }

  // create instruction
  Instr *instr = builder.AppendInstr(op, type);

  for (int i = 0; i < 3; i++) {
    if (!arg[i]) {
      continue;
    }

    instr->set_arg(i, arg[i]);
  }

  // insert instruction into slot if specified
  if (slot != -1) {
    slots_.insert(std::make_pair(slot, instr));
  }

  return true;
}
