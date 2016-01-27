#include <iomanip>
#include "jit/ir/ir_writer.h"

using namespace dvm::jit;
using namespace dvm::jit::ir;

void IRWriter::Print(const IRBuilder &builder) {
  value_ids_.clear();
  next_value_id_ = 0;

  std::stringstream ss;

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      PrintInstruction(ss, instr);
    }
  }

  LOG_INFO(ss.str().c_str());
}

void IRWriter::PrintType(std::stringstream &ss, ValueTy type) const {
  switch (type) {
    case VALUE_I8:
      ss << "i8";
      break;

    case VALUE_I16:
      ss << "i16";
      break;

    case VALUE_I32:
      ss << "i32";
      break;

    case VALUE_I64:
      ss << "i64";
      break;

    case VALUE_F32:
      ss << "f32";
      break;

    case VALUE_F64:
      ss << "f64";
      break;
  }
}

void IRWriter::PrintOpcode(std::stringstream &ss, Opcode op) const {
  const char *name = Opnames[op];

  while (*name) {
    ss << static_cast<char>(tolower(*name));
    name++;
  }
}

void IRWriter::PrintValue(std::stringstream &ss, const Value *value) {
  PrintType(ss, value->type());

  ss << " ";

  if (!value->constant()) {
    uintptr_t key = reinterpret_cast<uintptr_t>(value);
    auto it = value_ids_.find(key);

    if (it == value_ids_.end()) {
      auto res = value_ids_.insert(std::make_pair(key, next_value_id_++));
      it = res.first;
    }

    ss << "%%" << it->second;
  } else {
    switch (value->type()) {
      case VALUE_I8:
        ss << "0x" << std::hex << value->value<int8_t>() << std::dec;
        break;
      case VALUE_I16:
        ss << "0x" << std::hex << value->value<int16_t>() << std::dec;
        break;
      case VALUE_I32:
        ss << "0x" << std::hex << value->value<int32_t>() << std::dec;
        break;
      case VALUE_I64:
        ss << "0x" << std::hex << value->value<int64_t>() << std::dec;
        break;
      case VALUE_F32:
        ss << "0x" << std::hex << value->value<float>() << std::dec;
        break;
      case VALUE_F64:
        ss << "0x" << std::hex << value->value<double>() << std::dec;
        break;
    }
  }
}

void IRWriter::PrintInstruction(std::stringstream &ss, const Instr *instr) {
  // print result value if we have one
  if (instr->result()) {
    PrintValue(ss, instr->result());
    ss << " = ";
  }

  // print the actual opcode
  PrintOpcode(ss, instr->op());
  ss << " ";

  // print each argument
  bool need_comma = false;

  for (int i = 0; i < 3; i++) {
    if (!instr->arg(i)) {
      continue;
    }

    if (need_comma) {
      ss << ", ";
      need_comma = false;
    }

    PrintValue(ss, instr->arg(i));

    need_comma = true;
  }

  // print out any opcode flags
  if (instr->flags() & IF_INVALIDATE_CONTEXT) {
    ss << " INVALIDATE_CONTEXT";
  }

  ss << std::endl;
}
