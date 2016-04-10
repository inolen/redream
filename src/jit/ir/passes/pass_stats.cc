#include <vector>
#include "core/assert.h"
#include "jit/ir/passes/pass_stats.h"

using namespace re::jit::ir;
using namespace re::jit::ir::passes;

namespace re {
namespace jit {
namespace ir {
namespace passes {

static std::vector<const Stat *> *s_stats = nullptr;

static void RegisterStat(const Stat *stat) {
  // lazily initialize to avoid static initialization ordering problems
  if (!s_stats) {
    s_stats = new std::vector<const Stat *>();
  }

  s_stats->push_back(stat);
}

static void UnregisterStat(const Stat *stat) {
  auto it = std::find(s_stats->begin(), s_stats->end(), stat);
  CHECK_NE(it, s_stats->end());
  s_stats->erase(it);

  if (!s_stats->size()) {
    delete s_stats;
    s_stats = nullptr;
  }
}

Stat::Stat(const char *desc) : desc(desc), n(0) { RegisterStat(this); }

Stat::~Stat() { UnregisterStat(this); }

void DumpStats() {
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("Pass stats");
  LOG_INFO("===-----------------------------------------------------===");

  int w = 0;
  for (auto stat : *s_stats) {
    int l = static_cast<int>(strlen(stat->desc));
    w = std::max(l, w);
  }

  for (auto stat : *s_stats) {
    LOG_INFO("%-*s  %d", w, stat->desc, stat->n);
  }
}
}
}
}
}
