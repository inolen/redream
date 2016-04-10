#ifndef PASS_STATS_H
#define PASS_STATS_H

namespace re {
namespace jit {
namespace ir {
namespace passes {

#define DEFINE_STAT(name, desc) static Stat name(desc);

struct Stat {
  const char *desc;
  int n;

  Stat(const char *desc);
  ~Stat();

  operator int() const { return n; }

  const Stat &operator=(int v) {
    n = v;
    return *this;
  }

  const Stat &operator++(int v) {
    n++;
    return *this;
  }

  const Stat &operator+=(int v) {
    n += v;
    return *this;
  }

  const Stat &operator--() {
    n--;
    return *this;
  }

  const Stat &operator-=(int v) {
    n -= v;
    return *this;
  }
};

void DumpStats();
}
}
}
}

#endif
