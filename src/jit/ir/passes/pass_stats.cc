#include "core/assert.h"
#include "core/string.h"
#include "jit/ir/passes/pass_stats.h"

using namespace re::jit::ir;
using namespace re::jit::ir::passes;

namespace re {
namespace jit {
namespace ir {
namespace passes {

static Stat *s_head_stat;

static void RegisterStat(Stat *stat) {
  stat->next = s_head_stat;
  s_head_stat = stat;
}

static void UnregisterStat(Stat *stat) {
  Stat **tmp = &s_head_stat;

  while (*tmp) {
    Stat **next = &(*tmp)->next;

    if (*tmp == stat) {
      *tmp = *next;
      break;
    }

    tmp = next;
  }
}

Stat::Stat(const char *desc) : desc(desc), n(0), next(nullptr) {
  RegisterStat(this);
}

Stat::~Stat() { UnregisterStat(this); }

void DumpStats() {
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("Pass stats");
  LOG_INFO("===-----------------------------------------------------===");

  int w = 0;
  Stat *stat = s_head_stat;
  while (stat) {
    int l = static_cast<int>(strlen(stat->desc));
    w = std::max(l, w);
    stat = stat->next;
  }

  stat = s_head_stat;
  while (stat) {
    LOG_INFO("%-*s  %d", w, stat->desc, stat->n);
    stat = stat->next;
  }
}
}
}
}
}
