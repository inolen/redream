#ifndef SOURCE_MAP_H
#define SOURCE_MAP_H

#include <map>

namespace re {
namespace jit {

class SourceMap {
 public:
  void AddBlockAddress(uintptr_t host_addr, uint32_t guest_addr);
  bool LookupBlockAddress(uintptr_t host_addr, uint32_t *guest_addr);

  void AddLineAddress(uintptr_t host_addr, uint32_t guest_addr);
  bool LookupLineAddress(uintptr_t host_addr, uint32_t *guest_addr);

  void Reset();

 private:
  std::map<uintptr_t, uint32_t> block_addresses_;
  std::map<uintptr_t, uint32_t> line_addresses_;
};
}
}

#endif
