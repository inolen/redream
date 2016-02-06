#include <iomanip>
#include "jit/ir/ir_writer.h"

using namespace re::jit;
using namespace re::jit::ir;

void IRWriter::Print(const IRBuilder &builder, std::ostream &output) {
  slots_.clear();
  next_slot_ = 0;

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      PrintInstruction(instr, output);
    }
  }
}

void IRWriter::PrintType(ValueTy type, std::ostream &output) const {
  switch (type) {
    case VALUE_I8:
      output << "i8";
      break;

    case VALUE_I16:
      output << "i16";
      break;

    case VALUE_I32:
      output << "i32";
      break;

    case VALUE_I64:
      output << "i64";
      break;

    case VALUE_F32:
      output << "f32";
      break;

    case VALUE_F64:
      output << "f64";
      break;
  }
}

void IRWriter::PrintOpcode(Opcode op, std::ostream &output) const {
  const char *name = Opnames[op];

  while (*name) {
    output << static_cast<char>(tolower(*name));
    name++;
  }
}

void IRWriter::PrintValue(const Value *value, std::ostream &output) {
  PrintType(value->type(), output);

  output << " ";

  if (!value->constant()) {
    uintptr_t key = reinterpret_cast<uintptr_t>(value);
    auto it = slots_.find(key);

    if (it == slots_.end()) {
      auto res = slots_.insert(std::make_pair(key, next_slot_++));
      it = res.first;
    }

    output << "%" << it->second;
  } else {
    switch (value->type()) {
      case VALUE_I8:
        output << "0x" << std::hex << value->value<int8_t>() << std::dec;
        break;
      case VALUE_I16:
        output << "0x" << std::hex << value->value<int16_t>() << std::dec;
        break;
      case VALUE_I32:
        output << "0x" << std::hex << value->value<int32_t>() << std::dec;
        break;
      case VALUE_I64:
        output << "0x" << std::hex << value->value<int64_t>() << std::dec;
        break;
      case VALUE_F32:
        output << "0x" << std::hex << value->value<float>() << std::dec;
        break;
      case VALUE_F64:
        output << "0x" << std::hex << value->value<double>() << std::dec;
        break;
    }
  }
}

void IRWriter::PrintInstruction(const Instr *instr, std::ostream &output) {
  // print result value if we have one
  if (instr->result()) {
    PrintValue(instr->result(), output);
    output << " = ";
  }

  // print the actual opcode
  PrintOpcode(instr->op(), output);
  output << " ";

  // print each argument
  bool need_comma = false;

  for (int i = 0; i < 3; i++) {
    if (!instr->arg(i)) {
      continue;
    }

    if (need_comma) {
      output << ", ";
      need_comma = false;
    }

    PrintValue(instr->arg(i), output);

    need_comma = true;
  }

  output << std::endl;
}
