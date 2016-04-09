#include <fstream>
#include <memory>
#include <sstream>
#include <gflags/gflags.h>
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/ir_reader.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"

using namespace re;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

DEFINE_string(pass, "lse,dce", "Comma-separated list of passes to run");
DEFINE_bool(debug, false, "Enable debug spew for passes");

static std::vector<std::string> split(const std::string &s, char delim) {
  std::stringstream ss(s);
  std::string item;
  std::vector<std::string> elems;
  while (std::getline(ss, item, delim)) {
    elems.push_back(std::move(item));
  }
  return elems;
}

static int get_num_instrs(IRBuilder &builder) {
  int n = 0;

  for (auto instr : builder.instrs()) {
    ((void)instr);
    n++;
  }

  return n;
}

int main(int argc, const char **argv) {
  const char *file = argv[1];

  Arena arena(4096);
  IRBuilder builder(arena);

  // read in the input ir
  IRReader reader;
  std::ifstream input_stream(file);
  CHECK(reader.Parse(input_stream, builder));

  // run optimization passes
  std::vector<std::string> passes = split(FLAGS_pass, ',');
  for (auto name : passes) {
    std::unique_ptr<Pass> pass;

    int num_instrs_before = get_num_instrs(builder);

    if (name == "lse") {
      pass = std::unique_ptr<Pass>(new LoadStoreEliminationPass());
    } else if (name == "dce") {
      pass = std::unique_ptr<Pass>(new DeadCodeEliminationPass());
    } else {
      LOG_WARNING("Unknown pass %s", name.c_str());
    }
    pass->Run(builder, FLAGS_debug);

    int num_instrs_after = get_num_instrs(builder);

    // print out the resulting ir
    LOG_INFO("%s:", pass->name());
    builder.Dump();

    // print out stats about the optimization pass
    if (num_instrs_after <= num_instrs_before) {
      int delta = num_instrs_before - num_instrs_after;
      LOG_INFO(ANSI_COLOR_GREEN "%d (%.2f%%) instructions removed" ANSI_COLOR_RESET,
        delta, (delta / static_cast<float>(num_instrs_before)) * 100.0f);
    } else {
      int delta = num_instrs_after - num_instrs_before;
      LOG_INFO(ANSI_COLOR_RED "%d (%.2f%%) instructions added" ANSI_COLOR_RESET,
        delta, (delta / static_cast<float>(num_instrs_before)) * 100.0f);
    }
    LOG_INFO("");
  }

  return 0;
}
