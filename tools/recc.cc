#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <gflags/gflags.h>
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/ir_reader.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "sys/filesystem.h"

using namespace re;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;
using namespace re::sys;

DEFINE_string(pass, "lse,dce", "Comma-separated list of passes to run");
DEFINE_bool(print_after_all, true, "Print IR after each pass");
DEFINE_bool(stats, true, "Display pass stats");

DEFINE_STAT(num_instrs, "Total number of instructions");
DEFINE_STAT(num_instrs_removed, "Number of instructions removed");

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

static void process_file(const char *filename, bool disable_ir_dump) {
  Arena arena(4096);
  IRBuilder builder(arena);

  // read in the input ir
  IRReader reader;
  std::ifstream input_stream(filename);
  CHECK(reader.Parse(input_stream, builder));

  int num_instrs_before = get_num_instrs(builder);

  // run optimization passes
  std::vector<std::string> passes = split(FLAGS_pass, ',');
  for (auto name : passes) {
    std::unique_ptr<Pass> pass;

    if (name == "lse") {
      pass = std::unique_ptr<Pass>(new LoadStoreEliminationPass());
    } else if (name == "dce") {
      pass = std::unique_ptr<Pass>(new DeadCodeEliminationPass());
    } else {
      LOG_WARNING("Unknown pass %s", name.c_str());
    }
    pass->Run(builder);

    // print IR after each pass if requested
    if (!disable_ir_dump && FLAGS_print_after_all) {
      LOG_INFO("===-----------------------------------------------------===");
      LOG_INFO("IR after %s", pass->name());
      LOG_INFO("===-----------------------------------------------------===");
      builder.Dump();
      LOG_INFO("");
    }
  }

  int num_instrs_after = get_num_instrs(builder);

  // print out the final IR
  if (!disable_ir_dump && !FLAGS_print_after_all) {
    builder.Dump();
    LOG_INFO("");
  }

  num_instrs += num_instrs_before;
  num_instrs_removed += num_instrs_before - num_instrs_after;
}

static void process_dir(const char *path) {
  if (DIR *dir = opendir(path)) {
    while (struct dirent *ent = readdir(dir)) {
      if (!(ent->d_type & DT_REG)) {
        continue;
      }

      char filename[PATH_MAX];
      snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "%s", path,
               ent->d_name);

      LOG_INFO("processing %s", filename);

      process_file(filename, true);
    }

    closedir(dir);
  }
}

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  const char *path = argv[1];

  if (IsFile(path)) {
    process_file(path, false);
  } else {
    process_dir(path);
  }

  if (FLAGS_stats) {
    DumpStats();
  }

  google::ShutDownCommandLineFlags();

  return 0;
}
