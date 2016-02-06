#include "jit/source_map.h"

using namespace re;
using namespace re::jit;

void SourceMap::AddBlockAddress(uintptr_t host_addr, uint32_t guest_addr) {
  block_addresses_.insert(std::make_pair(host_addr, guest_addr));
}

bool SourceMap::LookupBlockAddress(uintptr_t host_addr, uint32_t *guest_addr) {
  // find the first block who's address is greater than host_addr
  auto it = block_addresses_.upper_bound(host_addr);

  // if all addresses are are greater than host_addr, there is no
  // block for this address
  if (it == block_addresses_.begin()) {
    return false;
  }

  // host_addr belongs to the block before
  *guest_addr = (--it)->second;

  return true;
}

void SourceMap::AddLineAddress(uintptr_t host_addr, uint32_t guest_addr) {
  line_addresses_.insert(std::make_pair(host_addr, guest_addr));
}

bool SourceMap::LookupLineAddress(uintptr_t host_addr, uint32_t *guest_addr) {
  auto it = line_addresses_.upper_bound(host_addr);

  if (it == line_addresses_.begin()) {
    return false;
  }

  *guest_addr = (--it)->second;

  return true;
}

void SourceMap::Reset() {
  block_addresses_.clear();
  line_addresses_.clear();
}
