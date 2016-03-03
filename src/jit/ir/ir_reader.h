#ifndef IR_READER_H
#define IR_READER_H

#include <istream>
#include <unordered_map>
#include "jit/ir/ir_builder.h"

namespace re {
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
  uint64_t i;
  ValueType ty;
};

class IRLexer {
 public:
  IRLexer(std::istream &input);

  IRToken tok() const { return tok_; }
  const IRLexeme &val() const { return val_; }

  IRToken Next();

 private:
  char Get();
  void Unget();

  std::istream &input_;
  IRToken tok_;
  IRLexeme val_;
};

class IRReader {
 public:
  bool Parse(std::istream &input, IRBuilder &builder);

 private:
  bool ParseType(IRLexer &lex, IRBuilder &builder, ValueType *type);
  bool ParseOp(IRLexer &lex, IRBuilder &builder, Op *op);
  bool ParseValue(IRLexer &lex, IRBuilder &builder, Value **value);
  bool ParseOperator(IRLexer &lex, IRBuilder &builder);
  bool ParseInstruction(IRLexer &lex, IRBuilder &builder);

  std::unordered_map<int, Value *> slots_;
};
}
}
}

#endif
