#ifndef IR_READER_H
#define IR_READER_H

#include <sstream>
#include <unordered_map>
#include "jit/ir/ir_builder.h"

namespace dvm {
namespace jit {
namespace ir {

enum IRToken {
  TOK_EOF,
  TOK_EOL,
  TOK_COMMA,
  TOK_OPERATOR,
  TOK_TYPE,
  TOK_INTEGER,
  TOK_IDENTIFIER,
};

struct IRLexeme {
  char s[128];
  uint8_t i8;
  uint16_t i16;
  uint32_t i32;
  uint32_t i64;
  float f32;
  double f64;
  ValueTy ty;
};

class IRLexer {
 public:
  IRLexer(std::istringstream &input);

  IRToken tok() const { return tok_; }
  const IRLexeme &val() const { return val_; }

  IRToken Next();

 private:
  char Get();
  void Unget();

  std::istringstream &input_;
  IRToken tok_;
  IRLexeme val_;
};

class IRReader {
 public:
  bool Parse(std::istringstream &input, IRBuilder &builder);

 private:
  bool ParseType(IRLexer &lex, IRBuilder &builder, ValueTy *type);
  bool ParseOpcode(IRLexer &lex, IRBuilder &builder, Opcode *op);
  bool ParseValue(IRLexer &lex, IRBuilder &builder, Value **value);
  bool ParseOperator(IRLexer &lex, IRBuilder &builder);
  bool ParseInstruction(IRLexer &lex, IRBuilder &builder);

  std::unordered_map<int, Value *> slots_;
};
}
}
}

#endif
