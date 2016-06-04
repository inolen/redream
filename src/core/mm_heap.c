#include "core/assert.h"
#include "core/core.h"
#include "core/mm_heap.h"

static inline bool mm_is_max_level(int index) {
  int n = index + 1;
  int log2 = 0;
  while (n >>= 1) log2++;
  return log2 % 2 == 1;
}

static inline int mm_parent(int index) {
  return (index - 1) / 2;
}

static inline int mm_grandparent(int index) {
  return mm_parent(mm_parent(index));
}

static inline bool mm_has_grandparent(int index) {
  return mm_parent(index) != 0;
}

static inline int mm_left_child(int index) {
  return 2 * index + 1;
}

static inline int mm_left_grandchild(int index) {
  return mm_left_child(mm_left_child(index));
}

static inline int mm_is_child(int parent, int child) {
  return parent == ((child - 1) / 2);
}

static void mm_sift_up(mm_type *begin, int size, int index, mm_cmp cmp) {
  // can't sift up past the root
  if (!index) {
    return;
  }

  int ancestor_index = mm_parent(index);
  bool max_level = mm_is_max_level(ancestor_index);

  // if the node is smaller (greater) than its parent, then it is smaller
  // (greater) than all other nodes at max (min) levels up to the root. swap
  // the node with its parent and check min (max) levels up to the root until
  // the min-max order property is satisfied
  if (cmp(*(begin + index), *(begin + ancestor_index)) ^ max_level) {
    SWAP(*(begin + ancestor_index), *(begin + index));
    index = ancestor_index;
  }
  // if the node is greater (smaller) than its parent, then it is greater
  // (smaller) than all other nodes at min (max) levels up to the root. the
  // node is in the correct order with regards to its parent, but check max
  // (min) levels up to the root until the min-max order property is satisfied
  else {
    max_level = !max_level;
  }

  while (mm_has_grandparent(index)) {
    ancestor_index = mm_grandparent(index);

    // once node is greater (smaller) than parent, the min-max order property
    // is satisfied
    if (!(cmp(*(begin + index), *(begin + ancestor_index)) ^ max_level)) {
      break;
    }

    // swap node with parent
    SWAP(*(begin + ancestor_index), *(begin + index));
    index = ancestor_index;
  }
}

static void mm_sift_down(mm_type *begin, int size, int index, mm_cmp cmp) {
  bool max_level = mm_is_max_level(index);

  while (index < size) {
    // get the smallest (largest) child or grandchild
    int smallest = index;

    int i = mm_left_child(index);
    int end = MIN(i + 2, size);
    for (; i < end; i++) {
      if (cmp(*(begin + i), *(begin + smallest)) ^ max_level) {
        smallest = i;
      }
    }

    i = mm_left_grandchild(index);
    end = MIN(i + 4, size);
    for (; i < end; i++) {
      if (cmp(*(begin + i), *(begin + smallest)) ^ max_level) {
        smallest = i;
      }
    }

    // already the smallest (largest) node, nothing to do
    if (smallest == index) {
      break;
    }

    // swap the node with the smallest (largest) descendant
    SWAP(*(begin + index), *(begin + smallest));

    // if the swapped node was a child, then the current node, its child, and
    // its grandchild are all ordered correctly at this point satisfying the
    // min-max order property
    if (mm_is_child(index, smallest)) {
      break;
    }

    // if the node's new parent is now smaller than it, swap again
    int parent = mm_parent(smallest);
    if (cmp(*(begin + parent), *(begin + smallest)) ^ max_level) {
      SWAP(*(begin + parent), *(begin + smallest));
    }

    // if the swapped node was a grandchild, iteration must continue to
    // ensure it's now ordered with regard to its descendants
    index = smallest;
  }
}

bool mm_validate(mm_type *begin, int size, mm_cmp cmp) {
  for (int i = 0; i < size; i++) {
    bool flip_compare = mm_is_max_level(i);

    // values stored at nodes on even (odd) levels are smaller (greater) than
    // or equal to the values stored at their descendants

    // validate children
    int j = MIN(mm_left_child(i), size);
    int end = MIN(j + 2, size);

    for (; j < end; j++) {
      if (!(cmp(*(begin + i), *(begin + j)) ^ flip_compare)) {
        return false;
      }
    }

    // validate grandchildren
    j = MIN(mm_left_grandchild(i), size);
    end = MIN(j + 4, size);

    for (; j < end; j++) {
      if (!(cmp(*(begin + i), *(begin + j)) ^ flip_compare)) {
        return false;
      }
    }
  }

  return true;
}

void mm_push(mm_type *begin, int size, mm_cmp cmp) {
  mm_sift_up(begin, size, size - 1, cmp);
}

mm_type *mm_find_min(mm_type *begin, int size, mm_cmp cmp) {
  return begin;
}

mm_type *mm_find_max(mm_type *begin, int size, mm_cmp cmp) {
  if (size == 1) {
    // root must be the max
    return begin;
  } else if (size == 2) {
    // root's child must be the max
    return begin + 1;
  } else {
    // must be the larger of the two children
    if (cmp(*(begin + 1), *(begin + 2))) {
      return begin + 2;
    } else {
      return begin + 1;
    }
  }
}

void mm_pop_min(mm_type *begin, int size, mm_cmp cmp) {
  if (!size) {
    return;
  }

  mm_type *min = mm_find_min(begin, size, cmp);
  SWAP(*min, *(begin + size - 1));
  mm_sift_down(begin, size - 1, min - begin, cmp);
}

void mm_pop_max(mm_type *begin, int size, mm_cmp cmp) {
  if (!size) {
    return;
  }

  mm_type *max = mm_find_max(begin, size, cmp);
  SWAP(*max, *(begin + size - 1));
  mm_sift_down(begin, size - 1, max - begin, cmp);
}
